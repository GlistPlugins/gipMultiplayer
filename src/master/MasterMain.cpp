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
#include <stdexcept>

#include "sqlite3.h"

std::string GenerateRoomCode() {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    std::string code;
    for (int i = 0; i < 6; ++i) {
        code += charset[dis(gen)];
    }
    return code;
}

// Two rooms sharing a code would make joins land on whichever came first.
std::string GenerateUniqueRoomCode(const std::vector<gServerInfo>& serverList) {
    for (int attempt = 0; attempt < 32; attempt++) {
        std::string code = GenerateRoomCode();
        bool taken = false;
        for (const auto& s : serverList) {
            if (s.roomCode == code) { taken = true; break; }
        }
        if (!taken) return code;
    }
    return GenerateRoomCode();
}

// sqlite hands back null for a NULL column, which std::string cannot take.
std::string ColumnText(sqlite3_stmt* stmt, int column) {
    const unsigned char* text = sqlite3_column_text(stmt, column);
    return text ? reinterpret_cast<const char*>(text) : "";
}

// Renders a candidate list for logging.
std::string JoinCandidates(const std::vector<std::string>& candidates) {
    if (candidates.empty()) return "(none)";
    std::string out;
    for (size_t i = 0; i < candidates.size(); i++) {
        if (i) out += ", ";
        out += candidates[i];
    }
    return out;
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

        // Observed address first, then every network the host reports.
        std::vector<std::string> candidates;
        candidates.push_back(actualIp);
        for (const auto& local : p->localIps) {
            if (local != actualIp) candidates.push_back(local);
        }

        for (auto& s : serverList) {
            if (s.ip == actualIp) {
                s.name = p->name;
                s.currentPlayers = p->currentPlayers;
                s.maxPlayers = p->maxPlayers;
                s.matchState = p->matchState;
                s.isPrivate = p->isPrivate;
                s.hasPassword = p->hasPassword;
                s.isDedicated = p->isDedicated;
                s.useP2P = p->useP2P;
                s.hostSession = weakSession;
                s.lastHeartbeat = 0.0f;
                s.peerCandidates = candidates;
                assignedRoomCode = s.roomCode;
                found = true;
                break;
            }
        }
        if (!found) {
            assignedRoomCode = GenerateUniqueRoomCode(serverList);
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
            newServer.useP2P = p->useP2P;
            newServer.roomCode = assignedRoomCode;
            newServer.hostSession = weakSession;
            newServer.peerCandidates = candidates;
            serverList.push_back(newServer);
        }
        std::cout << "[MasterServer] Registered server: " << p->name << " (" << actualIp << ") State: " << p->matchState << " Room: " << assignedRoomCode << " Dedicated: " << (p->isDedicated ? "YES" : "NO") << " P2P: " << (p->useP2P ? "YES" : "NO") << " Candidates: " << JoinCandidates(candidates) << std::endl;

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

        bool matched = false;
        for (auto& s : serverList) {
            if (s.ip == actualIp) {
                s.lastHeartbeat = 0.0f;
                matched = true;
                break;
            }
        }
        // This host is ageing out while believing it is listed.
        if (!matched) {
            std::cout << "[MasterServer] Heartbeat from unregistered " << actualIp
                      << ", it will not stay listed" << std::endl;
        }
    }

    void OnPacket(std::shared_ptr<gMasterGetListPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        auto res = std::make_shared<gMasterSendListPacket>();

        std::string clientPublicIp = session->remote_address()->readable();
        size_t cColon = clientPublicIp.find(':');
        if (cColon != std::string::npos) clientPublicIp = clientPublicIp.substr(0, cColon);

        for (const auto& s : serverList) {
            if (!s.isPrivate && (p->matchStateFilter == -1 || s.matchState == p->matchStateFilter)) {
                gServerInfo copy = s;
                std::string serverPublicIp = s.ip;
                size_t sColon = serverPublicIp.find(':');
                if (sColon != std::string::npos) serverPublicIp = serverPublicIp.substr(0, sColon);

                if (s.isDedicated && serverPublicIp == clientPublicIp && s.peerCandidates.size() > 1) {
                    // Same NAT as the host, which usually will not hairpin.
                    copy.ip = s.peerCandidates[1];
                    std::cout << "[MasterServer] Same NAT as host " << s.roomCode
                              << ", sending private address " << copy.ip
                              << " instead of " << s.ip << std::endl;
                }
                res->servers.push_back(copy);
            }
        }
        std::cout << "[MasterServer] Server list to " << clientPublicIp << ": "
                  << res->servers.size() << " of " << serverList.size()
                  << " (filter " << p->matchStateFilter << ")" << std::endl;
        session->SendPacket(res);
    }

    void OnPacket(std::shared_ptr<gMasterPunchRequestPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        gServerInfo* targetServer = nullptr;

        // A same-NAT client is handed the host's private address, so match
        // candidates too, not just roomCode and the public ip.
        for (auto& s : serverList) {
            bool match = (s.roomCode == p->targetIdentifier || s.ip == p->targetIdentifier);
            if (!match) {
                for (const auto& candidate : s.peerCandidates) {
                    if (candidate == p->targetIdentifier) { match = true; break; }
                }
            }
            if (match) {
                targetServer = &s;
                break;
            }
        }

        // Fails if the host went away before the prune caught up.
        std::shared_ptr<znet::PeerSession> hostSession;
        if (targetServer) hostSession = targetServer->hostSession.lock();

        // These two need different fixes, so say which one happened.
        if (!targetServer) {
            std::cout << "[MasterServer] Punch request failed: no server matches \""
                      << p->targetIdentifier << "\". Known rooms: ";
            if (serverList.empty()) {
                std::cout << "(none)";
            } else {
                for (size_t i = 0; i < serverList.size(); i++) {
                    if (i) std::cout << ", ";
                    std::cout << serverList[i].roomCode << " @ " << serverList[i].ip;
                }
            }
            std::cout << std::endl;
            return;
        }

        if (!hostSession) {
            std::cout << "[MasterServer] Punch request failed: host for room "
                      << targetServer->roomCode << " (" << targetServer->ip
                      << ") registered but its control connection is gone; it has to reconnect"
                      << std::endl;
            return;
        }

        {
            std::string clientPublicIp = session->remote_address()->readable();
            size_t cColon = clientPublicIp.find(':');
            if (cColon != std::string::npos) clientPublicIp = clientPublicIp.substr(0, cColon);

            std::cout << "[MasterServer] Broker Handshake: Client(" << clientPublicIp << ":" << p->clientGamePort
                      << ") punching Host(" << targetServer->ip << ")" << std::endl;

            auto execHost = std::make_shared<gMasterPunchExecutePacket>();
            const std::string clientPublic = clientPublicIp + ":" + std::to_string(p->clientGamePort);
            execHost->peerCandidates.push_back(clientPublic);
            for (const auto& local : p->clientIps) {
                if (local != clientPublic) execHost->peerCandidates.push_back(local);
            }
            execHost->isHost = true;
            std::cout << "[MasterServer]   host punches to: " << JoinCandidates(execHost->peerCandidates) << std::endl;
            hostSession->SendPacket(execHost);

            auto execClient = std::make_shared<gMasterPunchExecutePacket>();
            execClient->peerCandidates = targetServer->peerCandidates;
            execClient->isHost = false;
            std::cout << "[MasterServer]   client punches to: " << JoinCandidates(execClient->peerCandidates) << std::endl;
            session->SendPacket(execClient);
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
                res->useP2P = s.useP2P;
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
        if (!db) {
            res->success = false;
            res->message = "Database unavailable.";
            session->SendPacket(res);
            return;
        }

        // Bound, never concatenated: these three strings come off the wire.
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO USERS (username, email, password) VALUES (?, ?, ?);";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            res->success = false;
            res->message = "Database error.";
            session->SendPacket(res);
            return;
        }
        sqlite3_bind_text(stmt, 1, p->username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, p->email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, p->password.c_str(), -1, SQLITE_TRANSIENT);

        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE) {
            res->success = true;
            res->message = "Registration successful!";
            std::cout << "[MasterServer] Registered new user: " << p->username << " (" << p->email << ")" << std::endl;
        } else if (rc == SQLITE_CONSTRAINT) {
            res->success = false;
            res->message = "Username or Email already exists.";
        } else {
            res->success = false;
            res->message = "Database error.";
            std::cerr << "[MasterServer] Register failed: " << sqlite3_errmsg(db) << std::endl;
        }
        session->SendPacket(res);
    }

    void OnPacket(std::shared_ptr<gMasterUserLoginPacket> p) {
        auto res = std::make_shared<gMasterUserLoginResPacket>();
        res->success = false;

        if (p->email.empty() || p->password.empty()) {
            res->message = "Incorrect email or password.";
            session->SendPacket(res);
            return;
        }
        if (!db) {
            res->message = "Database unavailable.";
            session->SendPacket(res);
            return;
        }

        // Matched in one query, and one message for both misses: telling the
        // client which half was wrong tells it which emails are registered.
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT username FROM USERS WHERE email = ? AND password = ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            res->message = "Database error.";
            session->SendPacket(res);
            return;
        }
        sqlite3_bind_text(stmt, 1, p->email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, p->password.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            res->success = true;
            res->message = "Login successful!";
            res->username = ColumnText(stmt, 0);
            std::cout << "[MasterServer] User logged in: " << res->username << std::endl;
        } else {
            res->message = "Incorrect email or password.";
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
    sqlite3* db = nullptr;

    gMasterServerApp(uint16_t p) : port(p) {}

    ~gMasterServerApp() override {
        server.reset();
        if (db) sqlite3_close(db);
    }

    void setup() override {
        std::cout << "[MasterServer] Starting on port " << port << "..." << std::endl;

        // Initialize SQLite Database
        int rc = sqlite3_open("users.db", &db);
        if (rc != SQLITE_OK) {
            std::cerr << "Can't open database: " << sqlite3_errmsg(db) << std::endl;
            // Every handler checks for null rather than calling into a broken handle.
            sqlite3_close(db);
            db = nullptr;
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
        const float deltaTime = static_cast<float>(appmanager->getElapsedTime());
        std::lock_guard<std::mutex> lk(listMutex);
        for (auto it = serverList.begin(); it != serverList.end(); ) {
            it->lastHeartbeat += deltaTime;
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
    }
};

int main(int argc, char **argv) {
    uint16_t port = 25010;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            try {
                int parsed = std::stoi(argv[++i]);
                if (parsed < 1 || parsed > 65535) throw std::out_of_range("port");
                port = static_cast<uint16_t>(parsed);
            } catch (const std::exception&) {
                std::cerr << "Invalid --port value, using " << port << std::endl;
            }
        }
    }
    gStartEngine(new gMasterServerApp(port), "MasterServer", G_LOOPMODE_NORMAL);
    return 0;
}
