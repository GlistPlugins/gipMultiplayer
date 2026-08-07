#include "gAppManager.h"
#include "gBaseApp.h"
#include "gMasterPackets.h"
#include "znet/server.h"
#include "znet/server_events.h"
#include "znet/worker_signal.h"

#include <vector>
#include <mutex>
#include <memory>
#include <iostream>
#include <thread>
#include <chrono>

class gMasterPacketHandler : public znet::PacketHandler<gMasterPacketHandler, gMasterRegisterPacket, gMasterHeartbeatPacket, gMasterGetListPacket> {
public:
    gMasterPacketHandler(znet::PeerSession* s, std::vector<gServerInfo>& sl, std::mutex& m) 
        : session(s), serverList(sl), listMutex(m) {}

    void OnPacket(std::shared_ptr<gMasterRegisterPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        bool found = false;
        for (auto& s : serverList) {
            if (s.ip == p->ip) {
                s.name = p->name;
                s.currentPlayers = p->currentPlayers;
                s.maxPlayers = p->maxPlayers;
                s.lastHeartbeat = 0.0f;
                found = true;
                break;
            }
        }
        if (!found) {
            serverList.push_back({p->ip, p->name, p->currentPlayers, p->maxPlayers, 0.0f});
        }
        std::cout << "[MasterServer] Registered server: " << p->name << " (" << p->ip << ")" << std::endl;
    }

    void OnPacket(std::shared_ptr<gMasterHeartbeatPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        for (auto& s : serverList) {
            if (s.ip == p->ip) {
                s.lastHeartbeat = 0.0f;
                break;
            }
        }
    }

    void OnPacket(std::shared_ptr<gMasterGetListPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        auto res = std::make_shared<gMasterSendListPacket>();
        res->servers = serverList;
        session->SendPacket(res);
    }

    void OnUnknown(std::shared_ptr<znet::Packet>) {}

private:
    znet::PeerSession* session;
    std::vector<gServerInfo>& serverList;
    std::mutex& listMutex;
};

static std::shared_ptr<znet::Codec> makeMasterCodec() {
    auto codec = std::make_shared<znet::Codec>();
    codec->Add(PACKET_GIP_MASTER_REGISTER, std::make_unique<gMasterRegisterSerializer>());
    codec->Add(PACKET_GIP_MASTER_HEARTBEAT, std::make_unique<gMasterHeartbeatSerializer>());
    codec->Add(PACKET_GIP_MASTER_GET_LIST, std::make_unique<gMasterGetListSerializer>());
    codec->Add(PACKET_GIP_MASTER_SEND_LIST, std::make_unique<gMasterSendListSerializer>());
    return codec;
}

class gMasterServerApp : public gBaseApp {
public:
    std::unique_ptr<znet::Server> server;
    std::vector<gServerInfo> serverList;
    std::mutex listMutex;
    uint16_t port = 25010;

    gMasterServerApp(uint16_t p) : port(p) {}

    void setup() override {
        std::cout << "[MasterServer] Starting on port " << port << "..." << std::endl;
        
        appmanager->setTargetFramerate(60);
        
        server = std::make_unique<znet::Server>(znet::ServerConfig{"0.0.0.0", port, std::chrono::seconds(10), znet::ConnectionType::TCP});
        
        server->SetEventCallback([this](znet::Event& ev) {
            znet::EventDispatcher d{ev};
            d.Dispatch<znet::IncomingClientConnectedEvent>([this](znet::IncomingClientConnectedEvent& e) {
                auto sess = e.session();
                sess->SetCodec(makeMasterCodec());
                sess->SetHandler(std::make_shared<gMasterPacketHandler>(sess.get(), serverList, listMutex));
                return false;
            });
        });
        
        server->Bind();
        server->Listen();
    }

    void update() override {
        std::lock_guard<std::mutex> lk(listMutex);
        for (auto it = serverList.begin(); it != serverList.end(); ) {
            it->lastHeartbeat += 0.016f;
            if (it->lastHeartbeat > 30.0f) {
                std::cout << "[MasterServer] Pruned dead server: " << it->ip << std::endl;
                it = serverList.erase(it);
            } else {
                ++it;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
};

int main(int argc, char **argv) {
    uint16_t port = 25010;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
    }
    gStartEngine(new gMasterServerApp(port), "MasterServer", G_LOOPMODE_NORMAL);
    return 0;
}
