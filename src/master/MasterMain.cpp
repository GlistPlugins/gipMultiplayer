#include "gAppManager.h"
#include "gBaseApp.h"
#include "gBaseCanvas.h"
#include "gFont.h"
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

class gMasterServerApp;

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
                s.matchState = p->matchState;
                s.lastHeartbeat = 0.0f;
                found = true;
                break;
            }
        }
        if (!found) {
            serverList.push_back({p->ip, p->name, p->currentPlayers, p->maxPlayers, p->matchState, 0.0f});
        }
        std::cout << "[MasterServer] Registered server: " << p->name << " (" << p->ip << ") State: " << p->matchState << std::endl;
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
        for (const auto& s : serverList) {
            if (p->matchStateFilter == -1 || s.matchState == p->matchStateFilter) {
                res->servers.push_back(s);
            }
        }
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
    bool isRunning = false;
    
    gBaseCanvas* canvas;

    gMasterServerApp(uint16_t p) : port(p) {}

    void setup() override;
    
    void update() override {
        if (!isRunning) return;

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
        
        // Let the engine manage sleep if using gui
    }

    void toggleServer() {
        if (isRunning) {
            server.reset();
            isRunning = false;
            
            std::lock_guard<std::mutex> lk(listMutex);
            serverList.clear();
        } else {
            std::cout << "[MasterServer] Starting on port " << port << "..." << std::endl;
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
            isRunning = true;
        }
    }
};

class MasterCanvas : public gBaseCanvas {
public:
    gMasterServerApp* app;
    gFont font;
    gFont smallFont;

    MasterCanvas(gMasterServerApp* app) : app(app) {}

    void setup() override {
        // Assume FreeSans.ttf exists in assets/ (can be copied from game_martyr)
        font.loadFont("FreeSans.ttf", 20);
        smallFont.loadFont("FreeSans.ttf", 14);
    }
    
    void update() override {}

    void draw() override {
        // Draw background
        setColor(30, 30, 40);
        gDrawRectangle(0, 0, getWidth(), getHeight(), true);

        setColor(255, 255, 255);
        font.drawText("Martyr Master Server", 50, 50);

        // Start/Stop Button
        setColor(app->isRunning ? 200 : 50, app->isRunning ? 50 : 200, 50);
        gDrawRectangle(50, 80, 450, 50, true);
        setColor(255, 255, 255);
        font.drawText(app->isRunning ? "Stop Server" : "Start Server", 210, 115);
        
        // Status text
        if (app->isRunning) {
            font.drawText("Server is RUNNING on port " + std::to_string(app->port), 50, 170);
            
            // Draw list headers
            setColor(70, 90, 160);
            gDrawRectangle(50, 190, 540, 30, true);
            setColor(255, 255, 255);
            smallFont.drawText("Server Name", 60, 210);
            smallFont.drawText("Players", 300, 210);
            smallFont.drawText("IP Address", 400, 210);
            
            // Draw list
            std::lock_guard<std::mutex> lk(app->listMutex);
            int y = 240;
            for (const auto& s : app->serverList) {
                smallFont.drawText(s.name, 60, y);
                smallFont.drawText(std::to_string(s.currentPlayers) + "/" + std::to_string(s.maxPlayers), 300, y);
                smallFont.drawText(s.ip, 400, y);
                y += 30;
                if (y > getHeight() - 30) break;
            }
        } else {
            font.drawText("Server is STOPPED.", 50, 170);
        }
    }

    void mousePressed(int x, int y, int button) override {}

    void mouseReleased(int x, int y, int button) override {
        // Start/Stop Button
        if (x >= 50 && x <= 500 && y >= 80 && y <= 130) {
            app->toggleServer();
        }
    }

    void charPressed(unsigned int codepoint) override {}
    void keyReleased(int key) override {}
};

void gMasterServerApp::setup() {
    appmanager->setTargetFramerate(60);
    canvas = new MasterCanvas(this);
    setCurrentCanvas(canvas);
}

int main(int argc, char **argv) {
    uint16_t port = 25010;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        }
    }
    // Make sure we request a window to be drawn
    gStartEngine(new gMasterServerApp(port), "MasterServer", G_LOOPMODE_NORMAL, 640, 480);
    return 0;
}
