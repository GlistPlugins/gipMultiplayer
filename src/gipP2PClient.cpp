#include "gipP2PClient.h"
#include "master/gMasterPackets.h"
#include "znet/client.h"
#include "znet/client_events.h"
#include "znet/p2p/dialer.h"
#include "znet/inet_addr.h"
#include <iostream>
#include <mutex>
#include <condition_variable>

gipP2PClient::gipP2PClient() {}
gipP2PClient::~gipP2PClient() {}

std::shared_ptr<znet::PeerSession> gipP2PClient::joinSession(const std::string& masterIp, uint16_t masterPort, const std::string& targetIdentifier, uint16_t localGamePort) {
    std::cout << "[P2PClient] Connecting to Broker at " << masterIp << ":" << masterPort << "..." << std::endl;
    
    auto masterClient = std::make_unique<znet::Client>(znet::ClientConfig{masterIp, masterPort, std::chrono::seconds(5), znet::ConnectionType::TCP});
    
    std::shared_ptr<znet::PeerSession> punchedSession = nullptr;
    std::mutex mtx;
    std::condition_variable cv;
    bool punchDone = false;

    auto codec = std::make_shared<znet::Codec>();
    codec->Add(PACKET_GIP_MASTER_PUNCH_REQ, std::make_unique<gMasterPunchRequestSerializer>());
    codec->Add(PACKET_GIP_MASTER_PUNCH_EXEC, std::make_unique<gMasterPunchExecuteSerializer>());

    class BrokerHandler : public znet::PacketHandler<BrokerHandler, gMasterPunchExecutePacket> {
    public:
        BrokerHandler(uint16_t lp, std::shared_ptr<znet::PeerSession>& ps, std::mutex& m, std::condition_variable& c, bool& pd) 
            : localPort(lp), punchedSession(ps), mtx(m), cv(c), punchDone(pd) {}

        void OnPacket(std::shared_ptr<gMasterPunchExecutePacket> p) {
            std::cout << "[P2PClient] Broker sent " << p->peerCandidates.size() << " candidates (isHost: " << p->isHost << ")" << std::endl;
            
            std::shared_ptr<znet::InetAddress> localAddr = znet::InetAddress::from("0.0.0.0", localPort);
            
            std::vector<std::shared_ptr<znet::InetAddress>> candidates;
            for (const auto& c : p->peerCandidates) {
                size_t colon = c.find(':');
                if (colon != std::string::npos) {
                    std::string ip = c.substr(0, colon);
                    uint16_t port = std::stoi(c.substr(colon + 1));
                    candidates.push_back(znet::InetAddress::from(ip, port));
                }
            }
            
            bool isInitiator = !p->isHost;
            
            // Execute the punch synchronously
            auto sess = znet::p2p::PunchSync(localAddr, candidates, isInitiator, znet::ConnectionType::ZDT, std::chrono::seconds(10));
            
            {
                std::lock_guard<std::mutex> lock(mtx);
                punchedSession = sess;
                punchDone = true;
            }
            cv.notify_one();
        }

        void OnUnknown(std::shared_ptr<znet::Packet>) {}

    private:
        uint16_t localPort;
        std::shared_ptr<znet::PeerSession>& punchedSession;
        std::mutex& mtx;
        std::condition_variable& cv;
        bool& punchDone;
    };

    masterClient->SetEventCallback([&](znet::Event& ev) {
        znet::EventDispatcher d{ev};
        d.Dispatch<znet::ClientConnectedToServerEvent>([&](znet::ClientConnectedToServerEvent& e) {
            auto sess = e.session();
            sess->SetCodec(codec);
            sess->SetHandler(std::make_shared<BrokerHandler>(localGamePort, punchedSession, mtx, cv, punchDone));
            
            auto req = std::make_shared<gMasterPunchRequestPacket>();
            req->targetIdentifier = targetIdentifier;
            req->clientGamePort = localGamePort;
            req->clientIp = znet::GetLocalAddress(znet::InetProtocolVersion::IPv4) + ":" + std::to_string(localGamePort);
            sess->SendPacket(req);
            
            return false;
        });
        d.Dispatch<znet::ClientDisconnectedFromServerEvent>([&](znet::ClientDisconnectedFromServerEvent& e) {
            {
                std::lock_guard<std::mutex> lock(mtx);
                punchDone = true;
            }
            cv.notify_one();
            return false;
        });
    });

    masterClient->Bind();
    masterClient->Connect();

    std::unique_lock<std::mutex> lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(15), [&]{ return punchDone; });
    
    masterClient->Disconnect();

    if (punchedSession) {
        std::cout << "[P2PClient] Punch successful!" << std::endl;
    } else {
        std::cout << "[P2PClient] Punch failed or timed out." << std::endl;
    }

    return punchedSession;
}
