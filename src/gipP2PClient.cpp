#include "gipP2PClient.h"
#include "master/gMasterPackets.h"
#include "znet/client.h"
#include "znet/client_events.h"
#include "znet/p2p/dialer.h"
#include "znet/inet_addr.h"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr auto BROKER_TIMEOUT = std::chrono::seconds(15);
constexpr auto PUNCH_TIMEOUT = std::chrono::seconds(10);
constexpr auto HANDSHAKE_TIMEOUT = std::chrono::seconds(10);

// Shared, not referenced: the broker link can outlive the wait below.
struct gBrokerReply {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool hasCandidates = false;
    bool isHost = false;
    std::vector<std::string> candidates;

    void finish() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_one();
    }
};

// Records and wakes only. Punching here would block the broker link's
// worker until it idle-times out.
class gBrokerHandler : public znet::PacketHandler<gBrokerHandler, gMasterPunchExecutePacket> {
public:
    explicit gBrokerHandler(std::shared_ptr<gBrokerReply> reply) : reply_(std::move(reply)) {}

    void OnPacket(std::shared_ptr<gMasterPunchExecutePacket> p) {
        std::cout << "[P2PClient] Broker sent " << p->peerCandidates.size()
                  << " candidates (isHost: " << p->isHost << ")" << std::endl;
        {
            std::lock_guard<std::mutex> lock(reply_->mutex);
            reply_->candidates = p->peerCandidates;
            reply_->isHost = p->isHost;
            reply_->hasCandidates = true;
        }
        reply_->finish();
    }

    void OnUnknown(std::shared_ptr<znet::Packet>) {}

private:
    std::shared_ptr<gBrokerReply> reply_;
};

std::vector<std::shared_ptr<znet::InetAddress>> parseCandidates(const std::vector<std::string>& raw) {
    std::vector<std::shared_ptr<znet::InetAddress>> out;
    for (const auto& c : raw) {
        const size_t colon = c.find(':');
        if (colon == std::string::npos) continue;
        try {
            out.push_back(znet::InetAddress::from(c.substr(0, colon),
                                                  static_cast<uint16_t>(std::stoi(c.substr(colon + 1)))));
        } catch (const std::exception&) {
            std::cout << "[P2PClient] Ignoring malformed candidate: " << c << std::endl;
        }
    }
    return out;
}

// PunchSync returns mid-handshake; sending before it settles fails.
bool waitUntilReady(const std::shared_ptr<znet::PeerSession>& session) {
    const auto deadline = std::chrono::steady_clock::now() + HANDSHAKE_TIMEOUT;
    while (!session->IsReady() && session->IsAlive() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return session->IsReady() && session->IsAlive();
}

}  // namespace

gipP2PClient::gipP2PClient() {}
gipP2PClient::~gipP2PClient() {}

std::shared_ptr<znet::PeerSession> gipP2PClient::joinSession(const std::string& masterIp, uint16_t masterPort, const std::string& targetIdentifier, uint16_t localGamePort) {
    std::cout << "[P2PClient] Connecting to Broker at " << masterIp << ":" << masterPort << "..." << std::endl;

    auto reply = std::make_shared<gBrokerReply>();

    auto codec = std::make_shared<znet::Codec>();
    codec->Add(PACKET_GIP_MASTER_PUNCH_REQ, std::make_unique<gMasterPunchRequestSerializer>());
    codec->Add(PACKET_GIP_MASTER_PUNCH_EXEC, std::make_unique<gMasterPunchExecuteSerializer>());

    auto masterClient = std::make_unique<znet::Client>(znet::ClientConfig{masterIp, masterPort, std::chrono::seconds(5), znet::ConnectionType::TCP});

    masterClient->SetEventCallback([reply, codec, targetIdentifier, localGamePort](znet::Event& ev) {
        znet::EventDispatcher d{ev};
        d.Dispatch<znet::ClientConnectedToServerEvent>([&](znet::ClientConnectedToServerEvent& e) {
            auto sess = e.session();
            sess->SetCodec(codec);
            sess->SetHandler(std::make_shared<gBrokerHandler>(reply));

            auto req = std::make_shared<gMasterPunchRequestPacket>();
            req->targetIdentifier = targetIdentifier;
            req->clientGamePort = localGamePort;
            // Every local network, so a host sharing one has a path.
            const std::string port = ":" + std::to_string(localGamePort);
            for (const auto& local : znet::GetLocalAddresses(znet::InetProtocolVersion::IPv4)) {
                req->clientIps.push_back(local + port);
            }
            sess->SendPacket(req);
            return false;
        });
        d.Dispatch<znet::ClientDisconnectedFromServerEvent>([&](znet::ClientDisconnectedFromServerEvent& e) {
            reply->finish();
            return false;
        });
    });

    masterClient->Bind();
    masterClient->Connect();

    {
        std::unique_lock<std::mutex> lock(reply->mutex);
        reply->cv.wait_for(lock, BROKER_TIMEOUT, [&] { return reply->done; });
    }

    const bool hasCandidates = [&] {
        std::lock_guard<std::mutex> lock(reply->mutex);
        return reply->hasCandidates;
    }();

    // Only needed for the introduction.
    masterClient->Disconnect();
    masterClient.reset();

    if (!hasCandidates) {
        std::cout << "[P2PClient] Broker sent no candidates, giving up." << std::endl;
        return nullptr;
    }

    std::vector<std::string> rawCandidates;
    bool isHost = false;
    {
        std::lock_guard<std::mutex> lock(reply->mutex);
        rawCandidates = reply->candidates;
        isHost = reply->isHost;
    }

    const auto candidates = parseCandidates(rawCandidates);
    if (candidates.empty()) {
        std::cout << "[P2PClient] No usable candidates, giving up." << std::endl;
        return nullptr;
    }

    std::shared_ptr<znet::InetAddress> localAddr = znet::InetAddress::from("0.0.0.0", localGamePort);
    auto session = znet::p2p::PunchSync(localAddr, candidates, !isHost, znet::ConnectionType::ZDT, PUNCH_TIMEOUT);

    if (!session) {
        std::cout << "[P2PClient] Punch failed or timed out." << std::endl;
        return nullptr;
    }
    if (!waitUntilReady(session)) {
        std::cout << "[P2PClient] Punched session died during handshake." << std::endl;
        return nullptr;
    }

    std::cout << "[P2PClient] Punch successful!" << std::endl;
    return session;
}
