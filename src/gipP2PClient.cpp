#include "gipP2PClient.h"
#include "master/gMasterPackets.h"
#include "znet/client.h"
#include "znet/client_events.h"
#include "znet/inet_addr.h"
#include "znet/p2p/punch.h"
#include "znet/p2p/rendezvous.h"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <vector>

namespace {

constexpr auto GATHER_TIMEOUT = std::chrono::seconds(2);
constexpr auto BROKER_TIMEOUT = std::chrono::seconds(15);
constexpr auto PUNCH_TIMEOUT = std::chrono::seconds(10);

// One asynchronous step, waited for from the calling thread. Shared, not
// referenced: the host thread or the broker link can outlive the wait.
template <typename T>
struct gStep {
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    T value{};

    void finish(T v) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            value = std::move(v);
            done = true;
        }
        cv.notify_one();
    }

    void finish() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        cv.notify_one();
    }

    template <typename Duration>
    bool wait(Duration timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [&] { return done; });
    }

    T take() {
        std::lock_guard<std::mutex> lock(mutex);
        return value;
    }
};

struct gBrokerReply {
    bool hasCandidates = false;
    bool isHost = false;
    std::vector<znet::p2p::Candidate> candidates;
};

// Records and wakes only. Punching here would block the broker link's
// worker until it idle-times out.
class gBrokerHandler : public znet::PacketHandler<gBrokerHandler, gMasterPunchExecutePacket> {
public:
    explicit gBrokerHandler(std::shared_ptr<gStep<gBrokerReply>> reply) : reply_(std::move(reply)) {}

    void OnPacket(std::shared_ptr<gMasterPunchExecutePacket> p) {
        std::cout << "[P2PClient] Broker sent " << p->candidates.size()
                  << " candidates (isHost: " << p->isHost << ")" << std::endl;
        gBrokerReply reply;
        reply.hasCandidates = true;
        reply.isHost = p->isHost;
        reply.candidates = p->candidates;
        reply_->finish(std::move(reply));
    }

    void OnUnknown(std::shared_ptr<znet::Packet>) {}

private:
    std::shared_ptr<gStep<gBrokerReply>> reply_;
};

struct gPunchResult {
    znet::Result result = znet::Result::Failure;
    std::shared_ptr<znet::PeerSession> session;
};

}  // namespace

gipP2PClient::gipP2PClient() {}
gipP2PClient::~gipP2PClient() {}

gipP2PSession gipP2PClient::joinSession(const std::string& masterIp, uint16_t masterPort, uint16_t masterRelayPort,
                                        std::shared_ptr<znet::InetAddress> extraReflector,
                                        const std::string& targetIdentifier, uint16_t localGamePort) {
    // The socket everything happens on: the gathering, the punch and the
    // session afterwards, since a NAT hands out one mapping per socket.
    znet::p2p::HostConfig hostConfig;
    hostConfig.bind_address = "0.0.0.0";
    hostConfig.bind_port = localGamePort;
    auto host = std::make_unique<znet::p2p::Host>(hostConfig);
    if (host->Start() != znet::Result::Success) {
        std::cout << "[P2PClient] Could not open the punch socket on port " << localGamePort << std::endl;
        return {};
    }

    // Gather: the master's relay reflects the public mapping.
    auto gathered = std::make_shared<gStep<std::vector<znet::p2p::Candidate>>>();
    std::shared_ptr<znet::InetAddress> reflector = znet::InetAddress::from(masterIp, masterRelayPort);
    std::vector<std::shared_ptr<znet::InetAddress>> reflectors;
    if (reflector && reflector->is_valid()) reflectors.push_back(reflector);
    if (extraReflector && extraReflector->is_valid()) reflectors.push_back(extraReflector);
    host->Gather(reflectors, GATHER_TIMEOUT, [gathered](znet::p2p::Host::GatherResult result) {
        if (result.result != znet::Result::Success) {
            std::cout << "[P2PClient] Gather: " << znet::GetResultString(result.result) << ", offering the local addresses" << std::endl;
        }
        gathered->finish(std::move(result.candidates));
    });
    if (!gathered->wait(GATHER_TIMEOUT + std::chrono::seconds(1))) {
        std::cout << "[P2PClient] Gather never resolved, giving up." << std::endl;
        return {};
    }

    std::cout << "[P2PClient] Connecting to Broker at " << masterIp << ":" << masterPort << "..." << std::endl;

    auto reply = std::make_shared<gStep<gBrokerReply>>();

    auto codec = std::make_shared<znet::Codec>();
    codec->Add(PACKET_GIP_MASTER_PUNCH_REQ, std::make_unique<gMasterPunchRequestSerializer>());
    codec->Add(PACKET_GIP_MASTER_PUNCH_EXEC, std::make_unique<gMasterPunchExecuteSerializer>());

    auto masterClient = std::make_unique<znet::Client>(znet::ClientConfig{masterIp, masterPort, std::chrono::seconds(5), znet::ConnectionType::TCP});
    const std::vector<znet::p2p::Candidate> candidates = gathered->take();

    masterClient->SetEventCallback([reply, codec, targetIdentifier, localGamePort, candidates](znet::Event& ev) {
        znet::EventDispatcher d{ev};
        d.Dispatch<znet::ClientConnectedToServerEvent>([&](znet::ClientConnectedToServerEvent& e) {
            auto sess = e.session();
            sess->SetCodec(codec);
            sess->SetHandler(std::make_shared<gBrokerHandler>(reply));

            auto req = std::make_shared<gMasterPunchRequestPacket>();
            req->targetIdentifier = targetIdentifier;
            req->clientGamePort = localGamePort;
            req->candidates = candidates;
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
    reply->wait(BROKER_TIMEOUT);
    const gBrokerReply brokered = reply->take();
    // the broker names its own host as 0.0.0.0 on the relay candidate
    const std::shared_ptr<znet::InetAddress> masterAddress = masterClient->server_address();

    // Only needed for the introduction.
    masterClient->Disconnect();
    masterClient.reset();

    if (!brokered.hasCandidates) {
        std::cout << "[P2PClient] Broker sent no candidates, giving up." << std::endl;
        return {};
    }

    znet::p2p::PunchOffer offer;
    for (znet::p2p::Candidate candidate : brokered.candidates) {
        if (!candidate.address) continue;
        if (znet::p2p::IsUnspecifiedHost(*candidate.address) && masterAddress) {
            candidate.address = masterAddress->WithPort(candidate.address->port());
        }
        offer.candidates.push_back(std::move(candidate));
    }
    if (offer.candidates.empty()) {
        std::cout << "[P2PClient] No usable candidates, giving up." << std::endl;
        return {};
    }
    // the host accepts, so its options decide encryption and compression
    offer.is_initiator = !brokered.isHost;
    offer.timeout = PUNCH_TIMEOUT;

    auto punched = std::make_shared<gStep<gPunchResult>>();
    host->Punch(std::move(offer), [punched](znet::Result result, std::shared_ptr<znet::PeerSession> session) {
        gPunchResult out;
        out.result = result;
        out.session = std::move(session);
        punched->finish(std::move(out));
    });
    // the punch, then the handshake, each within PUNCH_TIMEOUT
    if (!punched->wait(PUNCH_TIMEOUT * 2 + std::chrono::seconds(1))) {
        std::cout << "[P2PClient] Punch never resolved, giving up." << std::endl;
        return {};
    }
    const gPunchResult result = punched->take();
    if (result.result != znet::Result::Success || !result.session) {
        std::cout << "[P2PClient] Punch failed: " << znet::GetResultString(result.result) << std::endl;
        return {};
    }

    std::cout << "[P2PClient] Punch successful via " << result.session->remote_address()->readable() << std::endl;
    gipP2PSession out;
    out.host = std::move(host);
    out.session = result.session;
    return out;
}
