#include "gAppManager.h"
#include "gBaseApp.h"
#include "gMasterPackets.h"
#include "znet/p2p/punch.h"
#include "znet/p2p/relay_server.h"
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
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "sqlite3.h"

#include <cxxopts.hpp>

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
std::string JoinCandidates(const std::vector<znet::p2p::Candidate>& candidates) {
    std::string out;
    for (const auto& candidate : candidates) {
        if (!out.empty()) out += ", ";
        out += znet::p2p::GetCandidateTypeString(candidate.type) + " " + candidate.address->readable();
    }
    return out.empty() ? "(none)" : out;
}

// "ip:port" into its halves; false when there is no port.
bool SplitHostPort(const std::string& text, std::string& host, uint16_t& port) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos) return false;
    try {
        const int parsed = std::stoi(text.substr(colon + 1));
        if (parsed < 0 || parsed > 65535) return false;
        host = text.substr(0, colon);
        port = static_cast<uint16_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// The candidates one side is told to punch: what the other side gathered,
// reflexive first, with the master's own observation of it standing in when
// no reflector answered, then its host addresses, then the relayed one.
std::vector<znet::p2p::Candidate> BuildOffer(const std::string& observedIp, uint16_t punchPort,
                                             const std::vector<znet::p2p::Candidate>& gathered,
                                             const znet::p2p::Candidate* relayed) {
    std::vector<znet::p2p::Candidate> out;
    for (const auto& candidate : gathered) {
        if (candidate.type == znet::p2p::CandidateType::Reflexive) out.push_back(candidate);
    }
    if (out.empty()) {
        znet::p2p::Candidate reflexive;
        reflexive.type = znet::p2p::CandidateType::Reflexive;
        reflexive.address = znet::InetAddress::from(observedIp, punchPort);
        if (reflexive.address) out.push_back(reflexive);
    }
    auto known = [&out](const znet::InetAddress& address) {
        for (const auto& offered : out) {
            if (offered.address->readable() == address.readable()) return true;
        }
        return false;
    };
    for (const auto& candidate : gathered) {
        if (candidate.type == znet::p2p::CandidateType::Host && !known(*candidate.address)) out.push_back(candidate);
    }
    if (out.size() > znet::p2p::kMaxCandidates - 1) out.resize(znet::p2p::kMaxCandidates - 1);
    if (relayed) out.push_back(*relayed);
    return out;
}

// Readable host addresses for same-NAT matching and the browser's private
// address substitution: the observed endpoint first, then the host's own
// networks from its typed candidates.
std::vector<std::string> PeerAddresses(const std::string& observed,
                                       const std::vector<znet::p2p::Candidate>& candidates) {
    std::vector<std::string> out;
    out.push_back(observed);
    for (const auto& candidate : candidates) {
        if (candidate.type != znet::p2p::CandidateType::Host || !candidate.address) continue;
        const std::string readable = candidate.address->readable();
        if (readable != observed) out.push_back(readable);
    }
    return out;
}

std::string JoinCandidates(const std::vector<std::string>& candidates) {
    if (candidates.empty()) return "(none)";
    std::string out;
    for (size_t i = 0; i < candidates.size(); i++) {
        if (i) out += ", ";
        out += candidates[i];
    }
    return out;
}

// Cryptographically secure random bytes via OpenSSL
std::string GenerateSecureRandomHex(size_t numBytes) {
    std::vector<uint8_t> bytes(numBytes);
    RAND_bytes(bytes.data(), static_cast<int>(numBytes));
    std::stringstream ss;
    for (size_t i = 0; i < numBytes; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(bytes[i]);
    }
    return ss.str();
}

std::string GenerateSecureSessionToken() {
    return GenerateSecureRandomHex(32); // 256-bit token
}

std::string GenerateRandomSalt(size_t numBytes = 16) {
    return GenerateSecureRandomHex(numBytes); // 128-bit salt
}

// Industry-Standard Slow Password Hashing: PBKDF2-HMAC-SHA256 with 600,000 iterations (OWASP standard)
constexpr int PBKDF2_ITERATIONS = 600000;

std::string HashPasswordPBKDF2(const std::string& password, const std::string& saltHex) {
    unsigned char hash[32];
    std::vector<uint8_t> saltBytes;
    saltBytes.reserve(saltHex.length() / 2);
    for (size_t i = 0; i + 1 < saltHex.length(); i += 2) {
        std::string byteString = saltHex.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::strtol(byteString.c_str(), nullptr, 16));
        saltBytes.push_back(byte);
    }
    if (saltBytes.empty()) {
        saltBytes.resize(16, 0);
    }

    PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.length()),
                      saltBytes.data(), static_cast<int>(saltBytes.size()),
                      PBKDF2_ITERATIONS, EVP_sha256(),
                      sizeof(hash), hash);

    std::stringstream ss;
    for (int i = 0; i < 32; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// Token-at-Rest Protection: Hash session tokens in the database
std::string HashSessionToken(const std::string& rawToken) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(rawToken.c_str()), rawToken.size(), hash);
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// Backward-compatibility legacy fallback
std::string HashPasswordWithSalt(const std::string& clientHash, const std::string& salt) {
    std::string combined = clientHash + ":" + salt;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.size(), hash);
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

void PruneExpiredSessions(sqlite3* db) {
    if (!db) return;
    // Expire sessions inactive for more than 30 days
    const char* pruneSql = "DELETE FROM SESSIONS WHERE last_used < datetime('now', '-30 days');";
    sqlite3_exec(db, pruneSql, 0, 0, nullptr);
}

struct IpRateLimit {
    std::vector<std::chrono::steady_clock::time_point> failedAttempts;
    std::chrono::steady_clock::time_point lockoutUntil = std::chrono::steady_clock::time_point::min();
};

class RateLimiter {
    std::mutex mtx;
    std::unordered_map<std::string, IpRateLimit> tracker;
public:
    bool isLockedOut(const std::string& ip, std::string& outMsg) {
        std::lock_guard<std::mutex> lock(mtx);
        auto now = std::chrono::steady_clock::now();
        auto it = tracker.find(ip);
        if (it != tracker.end()) {
            if (now < it->second.lockoutUntil) {
                auto remainingSec = std::chrono::duration_cast<std::chrono::seconds>(it->second.lockoutUntil - now).count();
                outMsg = "Too many failed attempts. Locked out for " + std::to_string(remainingSec) + "s.";
                return true;
            }
        }
        return false;
    }

    void recordFailedAttempt(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mtx);
        auto now = std::chrono::steady_clock::now();
        auto& entry = tracker[ip];

        // Prune attempts older than 60 seconds (1 minute sliding window)
        entry.failedAttempts.erase(
            std::remove_if(entry.failedAttempts.begin(), entry.failedAttempts.end(),
                [&](const std::chrono::steady_clock::time_point& tp) {
                    return std::chrono::duration_cast<std::chrono::seconds>(now - tp).count() > 60;
                }),
            entry.failedAttempts.end()
        );

        entry.failedAttempts.push_back(now);

        // 10 failed attempts in 1 minute -> 5 minute lockout
        if (entry.failedAttempts.size() >= 10) {
            entry.lockoutUntil = now + std::chrono::minutes(5);
            entry.failedAttempts.clear();
            std::cout << "[MasterServer] [Security] IP " << ip << " locked out for 5 minutes (10 failed attempts in 1 minute)." << std::endl;
        }
    }

    void recordSuccess(const std::string& ip) {
        std::lock_guard<std::mutex> lock(mtx);
        tracker.erase(ip);
    }
};

static RateLimiter gRateLimiter;

class gMasterPacketHandler : public znet::PacketHandler<gMasterPacketHandler, gMasterRegisterPacket, gMasterHeartbeatPacket, gMasterGetListPacket, gMasterPunchRequestPacket, gMasterQueryRoomPacket, gMasterUserLoginPacket, gMasterUserRegisterPacket, gMasterUserTokenLoginPacket, gMasterUserLogoutPacket> {
public:
    gMasterPacketHandler(const std::shared_ptr<znet::PeerSession>& s, std::vector<gServerInfo>& sl, std::mutex& m, sqlite3* db,
                         znet::p2p::RelayServer* relay, const std::string& relayHost)
        : session(s.get()), weakSession(s), serverList(sl), listMutex(m), db(db), relay(relay), relayHost(relayHost) {}

    void OnPacket(std::shared_ptr<gMasterRegisterPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);
        bool found = false;
        std::string assignedRoomCode = "";

        // Observed host from the session, advertised port from the packet.
        std::string ignoredHost;
        uint16_t advertisedPort = 25000;
        SplitHostPort(p->ip, ignoredHost, advertisedPort);
        std::string actualIp = session->remote_address()->WithPort(advertisedPort)->readable();

        // Observed endpoint first, then the host's own networks, for same-NAT
        // matching and the private-address substitution in the browser list.
        std::vector<std::string> candidates = PeerAddresses(actualIp, p->candidates);

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
                s.candidates = p->candidates;
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
            newServer.candidates = p->candidates;
            serverList.push_back(newServer);
        }
        std::cout << "[MasterServer] Registered server: " << p->name << " (" << actualIp << ") State: " << p->matchState << " Room: " << assignedRoomCode << " Dedicated: " << (p->isDedicated ? "YES" : "NO") << " P2P: " << (p->useP2P ? "YES" : "NO") << " Candidates: " << JoinCandidates(candidates) << std::endl;

        auto res = std::make_shared<gMasterRegisterResponsePacket>();
        res->roomCode = assignedRoomCode;
        session->SendPacket(res);
    }

    void OnPacket(std::shared_ptr<gMasterHeartbeatPacket> p) {
        std::lock_guard<std::mutex> lk(listMutex);

        std::string ignoredHost;
        uint16_t advertisedPort = 25000;
        SplitHostPort(p->ip, ignoredHost, advertisedPort);
        std::string actualIp = session->remote_address()->WithPort(advertisedPort)->readable();

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
            std::string hostPublicIp;
            uint16_t hostPunchPort = 0;
            SplitHostPort(targetServer->ip, hostPublicIp, hostPunchPort);

            std::cout << "[MasterServer] Broker Handshake: Client(" << clientPublicIp << ":" << p->clientGamePort
                      << ") punching Host(" << targetServer->ip << ")" << std::endl;

            // One relay pairing for the two, the same token on both sides, for
            // when no direct candidate answers.
            znet::p2p::Candidate relayed;
            const znet::p2p::Candidate* relayedPtr = nullptr;
            if (relay) {
                znet::p2p::RelayServer::Allocation allocation;
                const znet::Result result = relay->Allocate(allocation);
                if (result == znet::Result::Success) {
                    relayed.type = znet::p2p::CandidateType::Relayed;
                    relayed.address = znet::InetAddress::from(relayHost.empty() ? "0.0.0.0" : relayHost, relay->address()->port());
                    relayed.relay_token = allocation.token;
                    relayedPtr = &relayed;
                } else {
                    std::cout << "[MasterServer]   no relay for this pair: " << znet::GetResultString(result) << std::endl;
                }
            }

            auto execHost = std::make_shared<gMasterPunchExecutePacket>();
            execHost->candidates = BuildOffer(clientPublicIp, p->clientGamePort, p->candidates, relayedPtr);
            execHost->isHost = true;
            std::cout << "[MasterServer]   host punches to: " << JoinCandidates(execHost->candidates) << std::endl;
            hostSession->SendPacket(execHost);

            auto execClient = std::make_shared<gMasterPunchExecutePacket>();
            execClient->candidates = BuildOffer(hostPublicIp, hostPunchPort, targetServer->candidates,
                                                relayedPtr);
            execClient->isHost = false;
            std::cout << "[MasterServer]   client punches to: " << JoinCandidates(execClient->candidates) << std::endl;
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

    static bool IsValidEmail(const std::string& email) {
        if (email.length() < 3 || email.length() > 100) return false;
        size_t at = email.find('@');
        if (at == std::string::npos || at == 0 || at == email.length() - 1) return false;
        size_t dot = email.find('.', at);
        if (dot == std::string::npos || dot == at + 1 || dot == email.length() - 1) return false;
        return true;
    }

    static bool IsValidUsername(const std::string& username) {
        if (username.length() < 3 || username.length() > 20) return false;
        for (char c : username) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
        }
        return true;
    }

    void OnPacket(std::shared_ptr<gMasterUserRegisterPacket> p) {
        auto res = std::make_shared<gMasterUserRegisterResPacket>();

        std::string email = p->email;
        for (char& c : email) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (!IsValidUsername(p->username)) {
            res->success = false;
            res->message = "Username must be 3-20 characters (letters, numbers, _).";
            session->SendPacket(res);
            return;
        }
        if (!IsValidEmail(email)) {
            res->success = false;
            res->message = "Invalid email address format.";
            session->SendPacket(res);
            return;
        }
        if (p->password.empty()) {
            res->success = false;
            res->message = "Password is required.";
            session->SendPacket(res);
            return;
        }
        if (!db) {
            res->success = false;
            res->message = "Database unavailable.";
            session->SendPacket(res);
            return;
        }

        // Bound, never concatenated: these strings come off the wire.
        std::string salt = GenerateRandomSalt(16);
        std::string pbkdf2Password = HashPasswordPBKDF2(p->password, salt);

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO USERS (username, email, password, salt, algo) VALUES (?, ?, ?, ?, 'pbkdf2');";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            res->success = false;
            res->message = "Database error.";
            session->SendPacket(res);
            return;
        }
        sqlite3_bind_text(stmt, 1, p->username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, pbkdf2Password.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, salt.c_str(), -1, SQLITE_TRANSIENT);

        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE) {
            int64_t userId = sqlite3_last_insert_rowid(db);
            std::string token = CreateSessionForUser(userId);
            res->success = true;
            res->message = "Registration successful!";
            res->token = token;
            std::cout << "[MasterServer] Registered new user: " << p->username << " (" << email << ") with PBKDF2-HMAC-SHA256." << std::endl;
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

    std::string CreateSessionForUser(int64_t userId) {
        if (!db || userId <= 0) return "";
        std::string rawToken = GenerateSecureSessionToken();
        std::string tokenHash = HashSessionToken(rawToken);

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "INSERT INTO SESSIONS (user_id, token_hash) VALUES (?, ?);";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, userId);
            sqlite3_bind_text(stmt, 2, tokenHash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            return rawToken;
        }
        return "";
    }

    void OnPacket(std::shared_ptr<gMasterUserLoginPacket> p) {
        auto res = std::make_shared<gMasterUserLoginResPacket>();
        res->success = false;

        std::string clientIp = session->remote_address()->readable();
        size_t colon = clientIp.find(':');
        if (colon != std::string::npos) clientIp = clientIp.substr(0, colon);

        std::string lockMsg;
        if (gRateLimiter.isLockedOut(clientIp, lockMsg)) {
            res->message = lockMsg;
            session->SendPacket(res);
            std::cout << "[MasterServer] [Security] Blocked login attempt from locked-out IP: " << clientIp << std::endl;
            return;
        }

        std::string email = p->email;
        for (char& c : email) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (email.empty() || p->password.empty()) {
            gRateLimiter.recordFailedAttempt(clientIp);
            res->message = "Incorrect email or password.";
            session->SendPacket(res);
            return;
        }
        if (!db) {
            res->message = "Database unavailable.";
            session->SendPacket(res);
            return;
        }

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, username, password, salt, algo FROM USERS WHERE LOWER(email) = ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            res->message = "Database error.";
            session->SendPacket(res);
            return;
        }
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t userId = sqlite3_column_int64(stmt, 0);
            std::string username = ColumnText(stmt, 1);
            std::string storedPass = ColumnText(stmt, 2);
            std::string salt = ColumnText(stmt, 3);
            std::string algo = ColumnText(stmt, 4);
            sqlite3_finalize(stmt);

            bool passwordMatch = false;
            if (algo == "pbkdf2" && !salt.empty()) {
                std::string expectedHash = HashPasswordPBKDF2(p->password, salt);
                passwordMatch = (expectedHash == storedPass);
            } else {
                // Auto-migration path for legacy accounts:
                // Test against legacy fast salted SHA256, legacy unsalted, and pre-hashed formats
                std::string legacySalted = HashPasswordWithSalt(p->password, salt);
                if (legacySalted == storedPass || storedPass == p->password) {
                    passwordMatch = true;
                } else {
                    // Also check if client passed SHA256-prehashed password historically
                    unsigned char shaBuf[SHA256_DIGEST_LENGTH];
                    SHA256(reinterpret_cast<const unsigned char*>(p->password.c_str()), p->password.size(), shaBuf);
                    std::stringstream ss;
                    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
                        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(shaBuf[i]);
                    }
                    std::string clientHash = ss.str();
                    if (HashPasswordWithSalt(clientHash, salt) == storedPass || storedPass == clientHash) {
                        passwordMatch = true;
                    }
                }

                if (passwordMatch) {
                    // Seamlessly upgrade to PBKDF2 (600,000 iterations) in database
                    std::string newSalt = GenerateRandomSalt(16);
                    std::string newPBKDF2Hash = HashPasswordPBKDF2(p->password, newSalt);
                    sqlite3_stmt* up = nullptr;
                    if (sqlite3_prepare_v2(db, "UPDATE USERS SET password = ?, salt = ?, algo = 'pbkdf2' WHERE id = ?;", -1, &up, nullptr) == SQLITE_OK) {
                        sqlite3_bind_text(up, 1, newPBKDF2Hash.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(up, 2, newSalt.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(up, 3, userId);
                        sqlite3_step(up);
                        sqlite3_finalize(up);
                        std::cout << "[MasterServer] [Security] Auto-upgraded user " << username << " to PBKDF2-HMAC-SHA256." << std::endl;
                    }
                }
            }

            if (passwordMatch) {
                gRateLimiter.recordSuccess(clientIp);
                std::string token = CreateSessionForUser(userId);
                res->success = true;
                res->message = "Login successful!";
                res->username = username;
                res->token = token;
                std::cout << "[MasterServer] User logged in: " << res->username << std::endl;
            } else {
                gRateLimiter.recordFailedAttempt(clientIp);
                res->message = "Incorrect email or password.";
            }
        } else {
            sqlite3_finalize(stmt);
            gRateLimiter.recordFailedAttempt(clientIp);
            res->message = "Incorrect email or password.";
        }
        session->SendPacket(res);
    }

    void OnPacket(std::shared_ptr<gMasterUserTokenLoginPacket> p) {
        auto res = std::make_shared<gMasterUserLoginResPacket>();
        res->success = false;

        std::string email = p->email;
        for (char& c : email) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        if (email.empty() || p->token.empty()) {
            res->message = "Invalid session token.";
            session->SendPacket(res);
            return;
        }
        if (!db) {
            res->message = "Database unavailable.";
            session->SendPacket(res);
            return;
        }

        // Active token expiry check: prune inactive tokens older than 30 days
        PruneExpiredSessions(db);

        std::string tokenHash = HashSessionToken(p->token);

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT U.username, S.id FROM SESSIONS S "
                          "JOIN USERS U ON S.user_id = U.id "
                          "WHERE LOWER(U.email) = ? AND (S.token_hash = ? OR S.token = ?);";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            res->message = "Database error.";
            session->SendPacket(res);
            return;
        }
        sqlite3_bind_text(stmt, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, tokenHash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, p->token.c_str(), -1, SQLITE_TRANSIENT); // for legacy unhashed session migration

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            res->success = true;
            res->message = "Auto-login successful!";
            res->username = ColumnText(stmt, 0);
            res->token = p->token;
            int64_t sessId = sqlite3_column_int64(stmt, 1);
            sqlite3_finalize(stmt);

            // Update last_used timestamp and ensure token_hash is stored
            sqlite3_stmt* upd = nullptr;
            if (sqlite3_prepare_v2(db, "UPDATE SESSIONS SET last_used = CURRENT_TIMESTAMP, token_hash = ? WHERE id = ?;", -1, &upd, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(upd, 1, tokenHash.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(upd, 2, sessId);
                sqlite3_step(upd);
                sqlite3_finalize(upd);
            }
            std::cout << "[MasterServer] User authenticated via session token: " << res->username << std::endl;
        } else {
            sqlite3_finalize(stmt);
            res->message = "Session expired or invalid. Please log in.";
            std::cout << "[MasterServer] Session token rejected for: " << email << std::endl;
        }
        session->SendPacket(res);
    }

    void OnPacket(std::shared_ptr<gMasterUserLogoutPacket> p) {
        if (!db || p->token.empty()) return;
        std::string tokenHash = HashSessionToken(p->token);

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "DELETE FROM SESSIONS WHERE token_hash = ? OR token = ?;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, tokenHash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, p->token.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            std::cout << "[MasterServer] Session revoked for token." << std::endl;
        }
    }

private:
    znet::PeerSession* session;  // owned by the session that owns this handler
    std::weak_ptr<znet::PeerSession> weakSession;
    std::vector<gServerInfo>& serverList;
    std::mutex& listMutex;
    sqlite3* db;
    znet::p2p::RelayServer* relay;  // null without one; owned by the app
    std::string relayHost;          // empty means this host
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
    codec->Add(PACKET_GIP_MASTER_USER_TOKEN_LOGIN, std::make_unique<gMasterUserTokenLoginSerializer>());
    codec->Add(PACKET_GIP_MASTER_USER_LOGOUT, std::make_unique<gMasterUserLogoutSerializer>());

    return codec;
}



struct gRelayOptions {
    bool enabled = false;
    // The host peers reach the relay at; empty means the one they reached the
    // master at, which the clients substitute.
    std::string publicHost;
    // The one UDP port the relay binds, forwards and reflects on.
    uint16_t port = 25011;
    // A second reflector on a distinct IP, so peers can tell a symmetric NAT
    // from a punchable one. Empty disables it. It only reflects; pairings and
    // allocations still run on the relay above. Bind it to the specific IP, not
    // 0.0.0.0, or its replies can carry the wrong source address.
    std::string reflector2Ip;
    uint16_t reflector2Port = 25011;
};

class gMasterServerApp : public gBaseApp {
public:
    std::unique_ptr<znet::Server> server;
    std::unique_ptr<znet::p2p::RelayServer> relay;
    // reflect-only; never pairs (see gRelayOptions::reflector2Ip)
    std::unique_ptr<znet::p2p::RelayServer> reflector2;
    std::vector<gServerInfo> serverList;
    std::mutex listMutex;
    uint16_t port = 25010;
    gRelayOptions relayOptions;
    sqlite3* db = nullptr;

    gMasterServerApp(uint16_t p, gRelayOptions relayOpts) : port(p), relayOptions(std::move(relayOpts)) {}

    ~gMasterServerApp() override {
        server.reset();
        relay.reset();
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
                              "salt TEXT NOT NULL DEFAULT '',"
                              "algo TEXT NOT NULL DEFAULT 'pbkdf2',"
                              "reg_date DATETIME DEFAULT CURRENT_TIMESTAMP);"
                              "CREATE TABLE IF NOT EXISTS SESSIONS ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "user_id INTEGER NOT NULL,"
                              "token_hash TEXT UNIQUE NOT NULL,"
                              "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
                              "last_used DATETIME DEFAULT CURRENT_TIMESTAMP,"
                              "FOREIGN KEY(user_id) REFERENCES USERS(id) ON DELETE CASCADE);";
            char* errMsg = 0;
            sqlite3_exec(db, sql, 0, 0, &errMsg);
            if (errMsg) {
                std::cerr << "SQL Error: " << errMsg << std::endl;
                sqlite3_free(errMsg);
            }
            // Safely attempt migrations if tables already existed
            sqlite3_exec(db, "ALTER TABLE USERS ADD COLUMN salt TEXT NOT NULL DEFAULT '';", 0, 0, nullptr);
            sqlite3_exec(db, "ALTER TABLE USERS ADD COLUMN algo TEXT NOT NULL DEFAULT 'pbkdf2';", 0, 0, nullptr);
            sqlite3_exec(db, "ALTER TABLE SESSIONS ADD COLUMN token_hash TEXT NOT NULL DEFAULT '';", 0, 0, nullptr);

            // Prune expired sessions on startup
            PruneExpiredSessions(db);
        }

        appmanager->setTargetFramerate(60);

        if (relayOptions.enabled) {
            znet::p2p::RelayServerConfig config;
            config.port = relayOptions.port;
            relay = std::make_unique<znet::p2p::RelayServer>(config);
            const znet::Result result = relay->Start();
            if (result != znet::Result::Success) {
                std::cerr << "[MasterServer] Relay failed to start: " << znet::GetResultString(result) << std::endl;
                relay.reset();
            } else {
                std::cout << "[MasterServer] Relay up on UDP port " << relayOptions.port << std::endl;
            }
        }

        // The optional second reflector; see gRelayOptions::reflector2Ip.
        if (!relayOptions.reflector2Ip.empty()) {
            znet::p2p::RelayServerConfig config;
            config.bind_address = relayOptions.reflector2Ip;
            config.port = relayOptions.reflector2Port;
            reflector2 = std::make_unique<znet::p2p::RelayServer>(config);
            const znet::Result result = reflector2->Start();
            if (result != znet::Result::Success) {
                std::cerr << "[MasterServer] Second reflector failed to start on " << relayOptions.reflector2Ip << ": " << znet::GetResultString(result) << std::endl;
                reflector2.reset();
            } else {
                std::cout << "[MasterServer] Second reflector up on " << relayOptions.reflector2Ip << ":" << relayOptions.reflector2Port << std::endl;
            }
        }

        server = std::make_unique<znet::Server>(znet::ServerConfig{"0.0.0.0", port, std::chrono::seconds(10), znet::ConnectionType::TCP});

        server->SetEventCallback([this](znet::Event& ev) {
            znet::EventDispatcher d{ev};
            d.Dispatch<znet::IncomingClientConnectedEvent>([this](znet::IncomingClientConnectedEvent& e) {
                auto sess = e.session();
                sess->SetCodec(makeMasterCodec());
                sess->SetHandler(std::make_shared<gMasterPacketHandler>(sess, serverList, listMutex, db,
                                                                        relay.get(), relayOptions.publicHost));
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
    cxxopts::Options options("MasterServer",
                             "gipMultiplayer master and broker server");
    options.add_options()
        ("port", "TCP port to broker on",
         cxxopts::value<uint16_t>()->default_value("25010"))
        ("relay", "Enable the embedded UDP relay fallback")
        ("relay-ip", "Public host peers reach the relay at; empty means the "
                     "host they reached the master at",
         cxxopts::value<std::string>()->default_value(""))
        ("relay-port", "UDP port the relay binds, forwards and reflects on",
         cxxopts::value<uint16_t>()->default_value("25011"))
        ("reflector2-ip", "A second reflector bound to this distinct local IP, "
                          "so peers can classify their NAT; empty disables it",
         cxxopts::value<std::string>()->default_value(""))
        ("reflector2-port", "UDP port the second reflector binds",
         cxxopts::value<uint16_t>()->default_value("25011"))
        ("h,help", "Show this help and exit");

    uint16_t port;
    gRelayOptions relay;
    try {
        const auto args = options.parse(argc, argv);
        if (args.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }
        port = args["port"].as<uint16_t>();
        relay.enabled = args.count("relay") > 0;
        relay.publicHost = args["relay-ip"].as<std::string>();
        relay.port = args["relay-port"].as<uint16_t>();
        relay.reflector2Ip = args["reflector2-ip"].as<std::string>();
        relay.reflector2Port = args["reflector2-port"].as<uint16_t>();
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "MasterServer: " << e.what() << std::endl;
        return 1;
    }

    gStartEngine(new gMasterServerApp(port, relay), "MasterServer", G_LOOPMODE_NORMAL);
    return 0;
}
