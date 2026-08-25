#include "NetworkManager.h"
#include "GameBackendLocal.h"
#include "GameBackendRemote.h"
#include <thread>
#include "NetworkSynchronizer.h" // For getLocalNodeId
#include <random>
#include "master/gMasterPackets.h"
#include "gipP2PClient.h"
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include "znet/inet_addr.h"
#include "znet/client.h"
#include "znet/client_events.h"

namespace {

const std::string MASTER_IP = "martyr.irrl.dev";
const uint16_t MASTER_PORT = 25010;
const uint16_t DEFAULT_GAME_PORT = 25000;
constexpr auto AUTH_TIMEOUT = std::chrono::seconds(15);

std::string hashPassword(const std::string& pwd) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(pwd.c_str()), pwd.size(), hash);
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// Splits "host" or "host:port", falling back to the default on anything odd.
void splitAddress(const std::string& address, std::string& outIp, uint16_t& outPort) {
    outIp = address;
    outPort = DEFAULT_GAME_PORT;
    const size_t colon = address.find(':');
    if (colon == std::string::npos) return;
    outIp = address.substr(0, colon);
    try {
        const int parsed = std::stoi(address.substr(colon + 1));
        if (parsed > 0 && parsed <= 65535) outPort = static_cast<uint16_t>(parsed);
    } catch (const std::exception&) {
        // Keep the default port.
    }
}

std::shared_ptr<znet::Codec> makeQueryCodec() {
    auto codec = std::make_shared<znet::Codec>();
    codec->Add(PACKET_SERVER_QUERY_REQ, std::make_unique<ServerQueryReqSerializer>());
    codec->Add(PACKET_SERVER_QUERY_RES, std::make_unique<ServerQueryResSerializer>());
    return codec;
}

std::shared_ptr<znet::Codec> makeMasterQueryCodec() {
    auto codec = std::make_shared<znet::Codec>();
    codec->Add(PACKET_GIP_MASTER_QUERY_ROOM, std::make_unique<gMasterQueryRoomSerializer>());
    codec->Add(PACKET_GIP_MASTER_QUERY_ROOM_RES, std::make_unique<gMasterQueryRoomResSerializer>());
    codec->Add(PACKET_GIP_MASTER_GET_LIST, std::make_unique<gMasterGetListSerializer>());
    codec->Add(PACKET_GIP_MASTER_SEND_LIST, std::make_unique<gMasterSendListSerializer>());
    return codec;
}

std::shared_ptr<znet::Codec> makeAuthCodec() {
    auto codec = std::make_shared<znet::Codec>();
    codec->Add(PACKET_GIP_MASTER_USER_LOGIN, std::make_unique<gMasterUserLoginSerializer>());
    codec->Add(PACKET_GIP_MASTER_USER_LOGIN_RES, std::make_unique<gMasterUserLoginResSerializer>());
    codec->Add(PACKET_GIP_MASTER_USER_REGISTER, std::make_unique<gMasterUserRegisterSerializer>());
    codec->Add(PACKET_GIP_MASTER_USER_REGISTER_RES, std::make_unique<gMasterUserRegisterResSerializer>());
    return codec;
}

std::string formatTeams(uint32_t maxPlayers) {
    return std::to_string(maxPlayers / 2) + "v" + std::to_string(maxPlayers / 2);
}

std::string formatSize(uint32_t current, uint32_t max) {
    return std::to_string(current) + "/" + std::to_string(max);
}

}  // namespace

NetworkManager* NetworkManager::getInstance() {
    static NetworkManager instance;
    return &instance;
}

std::shared_ptr<GameBackend> NetworkManager::getBackend() const {
    std::lock_guard<std::mutex> lk(backendMutex);
    return backend;
}

void NetworkManager::useBackend(std::shared_ptr<GameBackend> next) {
    beginJoin();
    wantsDisconnect = false;
    setBackend(std::move(next));
}

void NetworkManager::setBackend(std::shared_ptr<GameBackend> next) {
    std::lock_guard<std::mutex> lk(backendMutex);
    backend = std::move(next);
}

// Abandons any join still in flight, so its result is discarded instead of
// replacing whatever happens next.
uint64_t NetworkManager::beginJoin() {
    std::lock_guard<std::mutex> lk(backendMutex);
    backend.reset();
    return ++joinGeneration;
}

// Installs only if no newer attempt has started since.
bool NetworkManager::installBackend(std::shared_ptr<GameBackend> next, uint64_t generation) {
    std::lock_guard<std::mutex> lk(backendMutex);
    if (generation != joinGeneration) return false;
    backend = std::move(next);
    return true;
}

void NetworkManager::wireBackend(const std::shared_ptr<GameBackend>& next) {
    next->setOnLobbyStateUpdated([this](std::shared_ptr<LobbyStatePacket> p) {
        currentLobbyState = p;
        if (onLobbyStateUpdated) onLobbyStateUpdated(p);
    });
    next->setOnMatchStarted([this]() { if (onMatchStarted) onMatchStarted(); });
    next->setOnDisconnected([this]() { if (onDisconnected) onDisconnected(); });
    next->setOnKicked([this](std::string reason) { if (onKicked) onKicked(reason); });
}

void NetworkManager::disconnect() {
    wantsDisconnect = true;
}

void NetworkManager::update(float deltaTime) {
    if (wantsDisconnect) {
        setBackend(nullptr);
        wantsDisconnect = false;
        if (onDisconnected) onDisconnected();
    }

    // A local handle keeps it alive for this frame even if a join thread swaps
    // it, and means no lock is held while callbacks run.
    std::shared_ptr<GameBackend> active = getBackend();
    if (active) active->update(deltaTime);

    std::vector<QueryResult> batch;
    {
        std::lock_guard<std::mutex> lk(queryMutex);
        batch.swap(pendingQueries);
    }
    for (auto& q : batch) {
        if (onServerQueried) onServerQueried(q.name, q.format, q.sizeStr, q.ip, q.realIp, q.isDedicated, q.useP2P);
    }
}

void NetworkManager::hostLobby(const std::string& playerName, const std::string& name, uint8_t size, bool isPrivate, const std::string& password) {
    // Also abandons any join still in flight, which would otherwise install
    // itself over the lobby we are about to host.
    beginJoin();
    wantsDisconnect = false;
    NetworkSynchronizer::getInstance()->regenerateLocalNodeId();
    hostMode = true;
    localPlayerName = playerName;
    lobbyName = name;
    lobbyTeamSize = size;

    auto host = std::make_shared<GameBackendLocal>("0.0.0.0", DEFAULT_GAME_PORT);
    wireBackend(host);
    setBackend(host);
    host->start();

    // Bare host: the port is appended downstream, and the master replaces
    // this with the address it observes. Real addresses ride in localIps.
    host->registerWithMasterServer(name, isPrivate, password, MASTER_IP, MASTER_PORT,
                                   znet::GetLoopbackAddress(znet::InetProtocolVersion::IPv4));

    auto p = std::make_shared<LobbyJoinPacket>();
    p->playerName = localPlayerName;
    p->senderId = NetworkSynchronizer::getInstance()->getLocalNodeId();
    host->enqueuePacket(p); // Send instantly to self
}

void NetworkManager::joinLobby(const std::string& ipOrCode, const std::string& playerName, const std::string& password, bool forceDirect) {
    const uint64_t generation = beginJoin();
    wantsDisconnect = false;
    hostMode = false;
    NetworkSynchronizer::getInstance()->regenerateLocalNodeId();
    localPlayerName = playerName;

    std::thread([this, ipOrCode, password, forceDirect, generation]() {
        // Assign a random port to prevent conflicts when testing on the same computer
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(25001, 30000);
        const uint16_t clientPort = static_cast<uint16_t>(dis(gen));

        std::shared_ptr<GameBackend> joined;
        if (!forceDirect) {
            gipP2PClient client;
            if (auto sess = client.joinSession(MASTER_IP, MASTER_PORT, ipOrCode, clientPort)) {
                joined = std::make_shared<GameBackendRemote>(sess);
            }
        }
        if (!joined) {
            // Room codes have no dot in them, and there is nothing to dial.
            if (ipOrCode.find('.') == std::string::npos) return;
            std::string ip;
            uint16_t port = DEFAULT_GAME_PORT;
            splitAddress(ipOrCode, ip, port);
            joined = std::make_shared<GameBackendRemote>(ip, port);
        }

        wireBackend(joined);
        if (!installBackend(joined, generation)) return;
        joined->start();

        auto p = std::make_shared<LobbyJoinPacket>();
        p->playerName = localPlayerName;
        p->password = password;
        p->senderId = NetworkSynchronizer::getInstance()->getLocalNodeId();
        joined->sendPacket(p);
    }).detach();
}

void NetworkManager::toggleReady() {
    auto active = getBackend();
    if (!active) return;
    auto p = std::make_shared<ToggleReadyPacket>();
    p->senderId = NetworkSynchronizer::getInstance()->getLocalNodeId();
    if (hostMode) active->enqueuePacket(p); // Host modifies own queue
    else active->sendPacket(p);
}

void NetworkManager::switchTeam(uint8_t teamId) {
    auto active = getBackend();
    if (!active) return;
    auto p = std::make_shared<SwitchTeamPacket>();
    p->teamId = teamId;
    p->senderId = NetworkSynchronizer::getInstance()->getLocalNodeId();
    if (hostMode) active->enqueuePacket(p);
    else active->sendPacket(p);
}

void NetworkManager::startMatch() {
    auto active = getBackend();
    if (!hostMode || !active || active->roomPlayers.empty()) return;

    size_t ready = 0;
    for (const auto& rp : active->roomPlayers) {
        if (rp.isReady) ready++;
    }
    // Minus one because the host never clicks ready.
    if (ready < active->roomPlayers.size() - 1) return;

    auto p = std::make_shared<StartMatchPacket>();
    active->enqueuePacket(p); // Send to self to trigger onMatchStarted
    active->sendPacket(p); // Send to network
}

void NetworkManager::kickPlayer(uint32_t playerId) {
    auto active = getBackend();
    if (hostMode && active) {
        active->kickPlayer(playerId);
    }
}

void NetworkManager::pushQueryResult(const std::string& name, const std::string& format, const std::string& sizeStr,
                                     const std::string& ip, const std::string& realIp, bool isDedicated, bool useP2P) {
    std::lock_guard<std::mutex> lk(queryMutex);
    pendingQueries.push_back({name, format, sizeStr, ip, realIp, isDedicated, useP2P});
}

void NetworkManager::trackQueryClient(std::shared_ptr<znet::Client> client) {
    std::lock_guard<std::mutex> lk(queryMutex);
    queryClients.push_back(std::move(client));
}

void NetworkManager::clearQueries() {
    std::vector<std::shared_ptr<znet::Client>> stale;
    {
        std::lock_guard<std::mutex> lk(queryMutex);
        stale.swap(queryClients);
        pendingQueries.clear();
    }
    // Dropped outside the lock: tearing a client down can call back into us.
    stale.clear();
}

// Answers arrive on a network thread, so each handler only queues the result.
class QueryHandler : public znet::PacketHandler<QueryHandler, ServerQueryResPacket> {
    NetworkManager* nm;
    std::string ip;
    std::weak_ptr<znet::PeerSession> weakSess;
public:
    QueryHandler(NetworkManager* n, std::string ipAddr, const std::shared_ptr<znet::PeerSession>& s) : nm(n), ip(std::move(ipAddr)), weakSess(s) {}
    void OnPacket(std::shared_ptr<ServerQueryResPacket> p) {
        nm->pushQueryResult(p->lobbyName, p->format, p->sizeStr, ip, ip, p->isDedicated, false);
        if (auto s = weakSess.lock()) s->Close();
    }
    void OnUnknown(std::shared_ptr<znet::Packet>) {}
};

class RoomQueryHandler : public znet::PacketHandler<RoomQueryHandler, gMasterQueryRoomResPacket> {
    NetworkManager* nm;
    std::weak_ptr<znet::PeerSession> weakSess;
public:
    RoomQueryHandler(NetworkManager* n, const std::shared_ptr<znet::PeerSession>& s) : nm(n), weakSess(s) {}
    void OnPacket(std::shared_ptr<gMasterQueryRoomResPacket> p) {
        if (p->found) {
            nm->pushQueryResult(p->name, formatTeams(p->maxPlayers), formatSize(p->currentPlayers, p->maxPlayers),
                                p->roomCode, p->ip, p->isDedicated, p->useP2P);
        }
        if (auto s = weakSess.lock()) s->Close();
    }
    void OnUnknown(std::shared_ptr<znet::Packet>) {}
};

class ServerListHandler : public znet::PacketHandler<ServerListHandler, gMasterSendListPacket> {
    NetworkManager* nm;
    std::weak_ptr<znet::PeerSession> weakSess;
public:
    ServerListHandler(NetworkManager* n, const std::shared_ptr<znet::PeerSession>& s) : nm(n), weakSess(s) {}
    void OnPacket(std::shared_ptr<gMasterSendListPacket> p) {
        for (const auto& s : p->servers) {
            nm->pushQueryResult(s.name, formatTeams(s.maxPlayers), formatSize(s.currentPlayers, s.maxPlayers),
                                s.roomCode.empty() ? s.ip : s.roomCode, s.ip, s.isDedicated, s.useP2P);
        }
        if (auto sess = weakSess.lock()) sess->Close();
    }
    void OnUnknown(std::shared_ptr<znet::Packet>) {}
};

void NetworkManager::queryServer(const std::string& ip) {
    std::string actualIp;
    uint16_t actualPort = DEFAULT_GAME_PORT;
    splitAddress(ip, actualIp, actualPort);

    // Query server uses TCP on port + 1
    auto client = std::make_shared<znet::Client>(znet::ClientConfig{actualIp, static_cast<uint16_t>(actualPort + 1), std::chrono::seconds(2), znet::ConnectionType::TCP});
    client->SetEventCallback([this, ip](znet::Event& ev) {
        znet::EventDispatcher d{ev};
        d.Dispatch<znet::ClientConnectedToServerEvent>([this, ip](znet::ClientConnectedToServerEvent& e) {
            auto sess = e.session();
            sess->SetCodec(makeQueryCodec());
            sess->SetHandler(std::make_shared<QueryHandler>(this, ip, sess));
            sess->SendPacket(std::make_shared<ServerQueryReqPacket>());
            return false;
        });
    });
    client->Bind();
    client->Connect();
    trackQueryClient(std::move(client));
}

void NetworkManager::queryRoomCode(const std::string& roomCode) {
    auto client = std::make_shared<znet::Client>(znet::ClientConfig{MASTER_IP, MASTER_PORT, std::chrono::seconds(2), znet::ConnectionType::TCP});
    client->SetEventCallback([this, roomCode](znet::Event& ev) {
        znet::EventDispatcher d{ev};
        d.Dispatch<znet::ClientConnectedToServerEvent>([this, roomCode](znet::ClientConnectedToServerEvent& e) {
            auto sess = e.session();
            sess->SetCodec(makeMasterQueryCodec());
            sess->SetHandler(std::make_shared<RoomQueryHandler>(this, sess));

            auto req = std::make_shared<gMasterQueryRoomPacket>();
            req->roomCode = roomCode;
            sess->SendPacket(req);
            return false;
        });
    });
    client->Bind();
    client->Connect();
    trackQueryClient(std::move(client));
}

void NetworkManager::refreshGlobalServers() {
    auto client = std::make_shared<znet::Client>(znet::ClientConfig{MASTER_IP, MASTER_PORT, std::chrono::seconds(2), znet::ConnectionType::TCP});
    client->SetEventCallback([this](znet::Event& ev) {
        znet::EventDispatcher d{ev};
        d.Dispatch<znet::ClientConnectedToServerEvent>([this](znet::ClientConnectedToServerEvent& e) {
            auto sess = e.session();
            sess->SetCodec(makeMasterQueryCodec());
            sess->SetHandler(std::make_shared<ServerListHandler>(this, sess));
            sess->SendPacket(std::make_shared<gMasterGetListPacket>());
            return false;
        });
    });
    client->Bind();
    client->Connect();
    trackQueryClient(std::move(client));
}

std::string NetworkManager::authMessage() const {
    std::lock_guard<std::mutex> lk(authMutex);
    return authMessageText;
}

std::string NetworkManager::loggedInUsername() const {
    std::lock_guard<std::mutex> lk(authMutex);
    return authUsername;
}

void NetworkManager::clearAuthStatus() {
    setAuthResult(AUTH_NONE, "");
}

void NetworkManager::setAuthResult(AuthStatus status, const std::string& message, const std::string& username) {
    {
        std::lock_guard<std::mutex> lk(authMutex);
        authMessageText = message;
        if (!username.empty()) authUsername = username;
    }
    // Published last, so the UI never sees a settled status with a stale message.
    currentAuthStatus.store(status, std::memory_order_release);
}

class MasterAuthHandler : public znet::PacketHandler<MasterAuthHandler, gMasterUserLoginResPacket, gMasterUserRegisterResPacket> {
public:
    MasterAuthHandler(NetworkManager* m) : nm(m) {}
    void OnPacket(std::shared_ptr<gMasterUserLoginResPacket> p) {
        nm->setAuthResult(p->success ? NetworkManager::AUTH_SUCCESS : NetworkManager::AUTH_FAIL, p->message, p->username);
    }
    void OnPacket(std::shared_ptr<gMasterUserRegisterResPacket> p) {
        nm->setAuthResult(p->success ? NetworkManager::AUTH_SUCCESS : NetworkManager::AUTH_FAIL, p->message);
    }
private:
    NetworkManager* nm;
};

// Both auth calls are the same request/reply against the master, so they share
// one thread body and differ only in the packet they send.
void NetworkManager::runAuthRequest(const std::string& pendingMessage, std::function<std::shared_ptr<znet::Packet>()> makeRequest) {
    setAuthResult(AUTH_PENDING, pendingMessage);

    std::thread([this, makeRequest]() {
        auto client = std::make_shared<znet::Client>(znet::ClientConfig{MASTER_IP, MASTER_PORT, std::chrono::seconds(2), znet::ConnectionType::TCP});
        client->SetEventCallback([this, makeRequest](znet::Event& ev) {
            znet::EventDispatcher d{ev};
            d.Dispatch<znet::ClientConnectedToServerEvent>([this, makeRequest](znet::ClientConnectedToServerEvent& e) {
                auto sess = e.session();
                sess->SetCodec(makeAuthCodec());
                sess->SetHandler(std::make_shared<MasterAuthHandler>(this));
                sess->SendPacket(makeRequest());
                return false;
            });
            d.Dispatch<znet::ClientConnectionFailedEvent>([this](znet::ClientConnectionFailedEvent& e) {
                setAuthResult(AUTH_FAIL, "Failed to connect to Master Server.");
                return false;
            });
        });
        client->Bind();
        client->Connect();

        // The client has to outlive the reply, and a master that accepts but
        // never answers would otherwise strand this thread forever.
        const auto deadline = std::chrono::steady_clock::now() + AUTH_TIMEOUT;
        while (authStatus() == AUTH_PENDING && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (authStatus() == AUTH_PENDING) {
            setAuthResult(AUTH_FAIL, "Master Server did not respond.");
        }
        client->Disconnect();
    }).detach();
}

void NetworkManager::loginUser(const std::string& email, const std::string& password) {
    const std::string hashed = hashPassword(password);
    runAuthRequest("Logging in...", [email, hashed]() {
        auto req = std::make_shared<gMasterUserLoginPacket>();
        req->email = email;
        req->password = hashed;
        return req;
    });
}

void NetworkManager::registerUser(const std::string& username, const std::string& email, const std::string& password) {
    const std::string hashed = hashPassword(password);
    runAuthRequest("Registering...", [username, email, hashed]() {
        auto req = std::make_shared<gMasterUserRegisterPacket>();
        req->username = username;
        req->email = email;
        req->password = hashed;
        return req;
    });
}
