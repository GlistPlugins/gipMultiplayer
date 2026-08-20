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
#include <random>

#include "sqlite3.h"

std::string GenerateRoomCode() {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    std::string code;
    for (int i = 0; i < 6; ++i) {
        code += charset[dis(gen)];
    }
    return code;
}

class gMasterPacketHandler : public znet::PacketHandler<gMasterPacketHandler, gMasterRegisterPacket, gMasterHeartbeatPacket, gMasterGetListPacket, gMasterPunchRequestPacket, gMasterQueryRoomPacket, gMasterUserLoginPacket, gMasterUserRegisterPacket> {
public:
    gMasterPacketHandler(const std::shared_ptr<znet::PeerSession>& s, std::vector<gServerInfo>& sl, std::mutex& m, sqlite3* db)
        : session(s.get()), weakSession(s), serverList(sl), listMutex(m), db(db) {}

    void OnPacket(std::shared_ptr<gMasterRegisterPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        bool found = false;
        std::string assignedRoomCode = "";

        // Extract public IP from session
        std::string remoteIp = session->remote_address()->readable();
        size_t rColon = remoteIp.find(':');
        if (rColon != std::string::npos) remoteIp = remoteIp.substr(0, rColon);

        // Parse the provided port from p->ip
        std::string portStr = "25000";
        size_t colon = p->ip.find(':');
        if (colon != std::string::npos) portStr = p->ip.substr(colon + 1);
        std::string actualIp = remoteIp + ":" + portStr;

        for (auto& s : serverList) {
            if (s.ip == actualIp) {
                s.name = p->name;
                s.currentPlayers = p->currentPlayers;
                s.maxPlayers = p->maxPlayers;
                s.matchState = p->matchState;
                s.isPrivate = p->isPrivate;
                s.hasPassword = p->hasPassword;
                s.isDedicated = p->isDedicated;
                s.hostSession = weakSession;
                s.lastHeartbeat = 0.0f;
                s.peerCandidates.clear();
                s.peerCandidates.push_back(actualIp);
                s.peerCandidates.push_back(p->ip);
                assignedRoomCode = s.roomCode;
                found = true;
                break;
            }
        }
        if (!found) {
            assignedRoomCode = GenerateRoomCode();
            gServerInfo newServer;
            newServer.ip = actualIp;
            newServer.name = p->name;
            newServer.currentPlayers = p->currentPlayers;
            newServer.maxPlayers = p->maxPlayers;
            newServer.matchState = p->matchState;
            newServer.lastHeartbeat = 0.0f;
            newServer.isPrivate = p->isPrivate;
            newServer.hasPassword = p->hasPassword;
            newServer.isDedicated = p->isDedicated;
            newServer.roomCode = assignedRoomCode;
            newServer.hostSession = weakSession;
            newServer.peerCandidates.clear();
            newServer.peerCandidates.push_back(actualIp);
            newServer.peerCandidates.push_back(p->ip);
            serverList.push_back(newServer);
        }
        std::cout << "[MasterServer] Registered server: " << p->name << " (" << actualIp << ") State: " << p->matchState << " Room: " << assignedRoomCode << " Dedicated: " << (p->isDedicated ? "YES" : "NO") << std::endl;

        auto res = std::make_shared<gMasterRegisterResponsePacket>();
        res->roomCode = assignedRoomCode;
        session->SendPacket(res);
    }

    void OnPacket(std::shared_ptr<gMasterHeartbeatPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);

        std::string remoteIp = session->remote_address()->readable();
        size_t rColon = remoteIp.find(':');
        if (rColon != std::string::npos) remoteIp = remoteIp.substr(0, rColon);

        std::string portStr = "25000";
        size_t colon = p->ip.find(':');
        if (colon != std::string::npos) portStr = p->ip.substr(colon + 1);
        std::string actualIp = remoteIp + ":" + portStr;

        for (auto& s : serverList) {
            if (s.ip == actualIp) {
                s.lastHeartbeat = 0.0f;
                break;
            }
        }
    }

    void OnPacket(std::shared_ptr<gMasterGetListPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        auto res = std::make_shared<gMasterSendListPacket>();
        for (const auto& s : serverList) {
            if (!s.isPrivate && (p->matchStateFilter == -1 || s.matchState == p->matchStateFilter)) {
                res->servers.push_back(s);
            }
        }
        session->SendPacket(res);
    }

    void OnPacket(std::shared_ptr<gMasterPunchRequestPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        gServerInfo* targetServer = nullptr;

        for (auto& s : serverList) {
            if (s.roomCode == p->targetIdentifier || s.ip == p->targetIdentifier) {
                targetServer = &s;
                break;
            }
        }

        // lock() fails when the host has gone away since it registered, which is
        // the window the heartbeat prune has not caught up with yet.
        std::shared_ptr<znet::PeerSession> hostSession;
        if (targetServer) hostSession = targetServer->hostSession.lock();

        if (hostSession) {
            std::string clientPublicIp = session->remote_address()->readable();
            size_t cColon = clientPublicIp.find(':');
            if (cColon != std::string::npos) clientPublicIp = clientPublicIp.substr(0, cColon);

            std::cout << "[MasterServer] Broker Handshake: Client(" << clientPublicIp << ":" << p->clientGamePort
                      << ") punching Host(" << targetServer->ip << ")" << std::endl;

            auto execHost = std::make_shared<gMasterPunchExecutePacket>();
            execHost->peerCandidates.push_back(clientPublicIp + ":" + std::to_string(p->clientGamePort));
            if (!p->clientIp.empty()) execHost->peerCandidates.push_back(p->clientIp);
            execHost->isHost = true;
            hostSession->SendPacket(execHost);

            auto execClient = std::make_shared<gMasterPunchExecutePacket>();
            execClient->peerCandidates = targetServer->peerCandidates;
            execClient->isHost = false;
            session->SendPacket(execClient);
        } else {
            std::cout << "[MasterServer] Punch request failed: Server not found or host session dead." << std::endl;
        }
    }

    void OnUnknown(std::shared_ptr<znet::Packet>) {}

    void OnPacket(std::shared_ptr<gMasterQueryRoomPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        auto res = std::make_shared<gMasterQueryRoomResPacket>();
        res->roomCode = p->roomCode;
        res->found = false;
        for (const auto& s : serverList) {
            if (s.roomCode == p->roomCode) {
                res->found = true;
                res->name = s.name;
                res->currentPlayers = s.currentPlayers;
                res->maxPlayers = s.maxPlayers;
                res->hasPassword = s.hasPassword;
                res->isDedicated = s.isDedicated;
                res->ip = s.ip;
                break;
            }
        }
        session->SendPacket(res);
    }

    void OnPacket(std::shared_ptr<gMasterUserRegisterPacket> p) {
        auto res = std::make_shared<gMasterUserRegisterResPacket>();

        if (p->username.empty() || p->email.empty() || p->password.empty()) {
            res->success = false;
            res->message = "All fields are required.";
            session->SendPacket(res);
            return;
        }

        std::string sql = "INSERT INTO USERS (username, email, password) VALUES ('" + p->username + "', '" + p->email + "', '" + p->password + "');";
        char* errMsg = 0;
        int rc = sqlite3_exec(db, sql.c_str(), 0, 0, &errMsg);

        if (rc == SQLITE_OK) {
            res->success = true;
            res->message = "Registration successful!";
            std::cout << "[MasterServer] Registered new user: " << p->username << " (" << p->email << ")" << std::endl;
        } else {
            res->success = false;
            if (errMsg) {
                std::string errStr(errMsg);
                if (errStr.find("UNIQUE constraint failed") != std::string::npos) {
                    res->message = "Username or Email already exists.";
                } else {
                    res->message = "Database error: " + errStr;
                }
                sqlite3_free(errMsg);
            } else {
                res->message = "Unknown Database Error.";
            }
        }
        session->SendPacket(res);
    }

    void OnPacket(std::shared_ptr<gMasterUserLoginPacket> p) {
        auto res = std::make_shared<gMasterUserLoginResPacket>();

        std::string sql = "SELECT username, password FROM USERS WHERE email = '" + p->email + "';";
        sqlite3_stmt* stmt;
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0);

        if (rc == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                std::string dbUser = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                std::string dbPass = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

                if (dbPass == p->password) {
                    res->success = true;
                    res->message = "Login successful!";
                    res->username = dbUser;
                    std::cout << "[MasterServer] User logged in: " << dbUser << std::endl;
                } else {
                    res->success = false;
                    res->message = "Incorrect password.";
                }
            } else {
                res->success = false;
                res->message = "Email not found.";
            }
        } else {
            res->success = false;
            res->message = "Database error.";
        }
        sqlite3_finalize(stmt);
        session->SendPacket(res);
    }

private:
    znet::PeerSession* session;  // owned by the session that owns this handler
    std::weak_ptr<znet::PeerSession> weakSession;
    std::vector<gServerInfo>& serverList;
    std::mutex& listMutex;
    sqlite3* db;
};

#include "gDatabase.h"

static std::shared_ptr<znet::Codec> makeMasterCodec() {
    auto codec = std::make_shared<znet::Codec>();
    codec->Add(PACKET_GIP_MASTER_REGISTER, std::make_unique<gMasterRegisterSerializer>());
    codec->Add(PACKET_GIP_MASTER_HEARTBEAT, std::make_unique<gMasterHeartbeatSerializer>());
    codec->Add(PACKET_GIP_MASTER_GET_LIST, std::make_unique<gMasterGetListSerializer>());
    codec->Add(PACKET_GIP_MASTER_SEND_LIST, std::make_unique<gMasterSendListSerializer>());
    codec->Add(PACKET_GIP_MASTER_REGISTER_RES, std::make_unique<gMasterRegisterResponseSerializer>());
    codec->Add(PACKET_GIP_MASTER_PUNCH_REQ, std::make_unique<gMasterPunchRequestSerializer>());
    codec->Add(PACKET_GIP_MASTER_PUNCH_EXEC, std::make_unique<gMasterPunchExecuteSerializer>());
    codec->Add(PACKET_GIP_MASTER_QUERY_ROOM, std::make_unique<gMasterQueryRoomSerializer>());
    codec->Add(PACKET_GIP_MASTER_QUERY_ROOM_RES, std::make_unique<gMasterQueryRoomResSerializer>());

    // Auth
    codec->Add(PACKET_GIP_MASTER_USER_LOGIN, std::make_unique<gMasterUserLoginSerializer>());
    codec->Add(PACKET_GIP_MASTER_USER_LOGIN_RES, std::make_unique<gMasterUserLoginResSerializer>());
    codec->Add(PACKET_GIP_MASTER_USER_REGISTER, std::make_unique<gMasterUserRegisterSerializer>());
    codec->Add(PACKET_GIP_MASTER_USER_REGISTER_RES, std::make_unique<gMasterUserRegisterResSerializer>());

    return codec;
}



class gMasterServerApp : public gBaseApp {
public:
    std::unique_ptr<znet::Server> server;
    std::vector<gServerInfo> serverList;
    std::mutex listMutex;
    uint16_t port = 25010;
    sqlite3* db;

    gMasterServerApp(uint16_t p) : port(p) {}

    void setup() override {
        std::cout << "[MasterServer] Starting on port " << port << "..." << std::endl;

        // Initialize SQLite Database
        int rc = sqlite3_open("users.db", &db);
        if (rc) {
            std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
        } else {
            std::cout << "Opened users database successfully" << std::endl;
            const char* sql = "CREATE TABLE IF NOT EXISTS USERS ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "username TEXT UNIQUE NOT NULL,"
                              "email TEXT UNIQUE NOT NULL,"
                              "password TEXT NOT NULL,"
                              "reg_date DATETIME DEFAULT CURRENT_TIMESTAMP);";
            char* errMsg = 0;
            sqlite3_exec(db, sql, 0, 0, &errMsg);
            if (errMsg) {
                std::cerr << "SQL Error: " << errMsg << std::endl;
                sqlite3_free(errMsg);
            }
        }

        appmanager->setTargetFramerate(60);

        server = std::make_unique<znet::Server>(znet::ServerConfig{"0.0.0.0", port, std::chrono::seconds(10), znet::ConnectionType::TCP});

        server->SetEventCallback([this](znet::Event& ev) {
            znet::EventDispatcher d{ev};
            d.Dispatch<znet::IncomingClientConnectedEvent>([this](znet::IncomingClientConnectedEvent& e) {
                auto sess = e.session();
                sess->SetCodec(makeMasterCodec());
                sess->SetHandler(std::make_shared<gMasterPacketHandler>(sess, serverList, listMutex, db));
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
            // A host whose session has gone is unreachable now, so it goes
            // immediately rather than sitting in the list for the full timeout
            // and being handed out to clients that cannot punch to it.
            if (it->hostSession.expired()) {
                std::cout << "[MasterServer] Host disconnected, removed: " << it->ip << std::endl;
                it = serverList.erase(it);
            } else if (it->lastHeartbeat > 30.0f) {
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
