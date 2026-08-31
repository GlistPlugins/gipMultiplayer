#include "NetworkManager.h"
#include "GameBackendLocal.h"
#include "GameBackendRemote.h"
#include <thread>
#include "NetworkSynchronizer.h" // For getLocalNodeId
#include <random>
#include "master/gMasterPackets.h"
#include "gipP2PClient.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif
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

std::string getEnvOr(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return value ? value : fallback;
}

// User name, machine name and home directory, so the saved token only decrypts
// for the same user on the same machine.
std::string getMachineKeyMaterial() {
    std::string material = "gipMultiplayer_GameMartyr_SecToken_2026_";
#ifdef _WIN32
    material += getEnvOr("USERNAME");
    material += getEnvOr("COMPUTERNAME");
    material += getEnvOr("USERPROFILE");
#else
    const passwd* pw = getpwuid(getuid());
    material += getEnvOr("USER", pw && pw->pw_name ? pw->pw_name : "");
    char host[256] = {};
    if (gethostname(host, sizeof(host) - 1) == 0) material += host;
    material += getEnvOr("HOME", pw && pw->pw_dir ? pw->pw_dir : "");
#endif
    return material;
}

std::vector<uint8_t> encryptData(const std::string& plaintext) {
    uint8_t iv[16];
    RAND_bytes(iv, sizeof(iv));

    std::string keyMaterial = getMachineKeyMaterial();
    uint8_t key[32];
    SHA256(reinterpret_cast<const unsigned char*>(keyMaterial.data()), keyMaterial.size(), key);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv);

    std::vector<uint8_t> ciphertext(plaintext.size() + 32);
    int len = 0, ciphertext_len = 0;

    EVP_EncryptUpdate(ctx, ciphertext.data(), &len, reinterpret_cast<const unsigned char*>(plaintext.data()), static_cast<int>(plaintext.size()));
    ciphertext_len = len;

    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertext_len += len;
    EVP_CIPHER_CTX_free(ctx);

    ciphertext.resize(ciphertext_len);

    std::vector<uint8_t> out;
    out.reserve(16 + ciphertext.size());
    out.insert(out.end(), iv, iv + 16);
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    return out;
}

std::string decryptData(const std::vector<uint8_t>& encrypted) {
    if (encrypted.size() <= 16) return "";

    const uint8_t* iv = encrypted.data();
    const uint8_t* ciphertext = encrypted.data() + 16;
    int ciphertext_len = static_cast<int>(encrypted.size()) - 16;

    std::string keyMaterial = getMachineKeyMaterial();
    uint8_t key[32];
    SHA256(reinterpret_cast<const unsigned char*>(keyMaterial.data()), keyMaterial.size(), key);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv);

    std::vector<uint8_t> plaintext(ciphertext_len + 32);
    int len = 0, plaintext_len = 0;

    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext, ciphertext_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    plaintext_len = len;

    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }
    plaintext_len += len;
    EVP_CIPHER_CTX_free(ctx);

    return std::string(reinterpret_cast<char*>(plaintext.data()), plaintext_len);
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
    codec->Add(PACKET_GIP_MASTER_USER_TOKEN_LOGIN, std::make_unique<gMasterUserTokenLoginSerializer>());
    codec->Add(PACKET_GIP_MASTER_USER_LOGOUT, std::make_unique<gMasterUserLogoutSerializer>());
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
    next->setVoiceEnabled(voiceMode.load() != VOICE_MODE_OFF);
    next->setHearEnemiesVoice(hearEnemiesVoice.load());
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

int NetworkManager::getPing() const {
    auto active = getBackend();
    return active ? active->getPing() : 0;
}

void NetworkManager::setVoiceMode(VoiceChatMode mode) {
    voiceMode.store(mode, std::memory_order_release);
    auto active = getBackend();
    if (active) {
        if (mode == VOICE_MODE_OFF) {
            active->stopVoiceTransmission();
            active->setVoiceEnabled(false);
        } else {
            active->setVoiceEnabled(true);
        }
    }
}

void NetworkManager::setProximityChatEnabled(bool enabled) {
    proximityChatEnabled.store(enabled, std::memory_order_release);
    setHearEnemiesVoice(enabled);
}

float NetworkManager::calculateProximityVolume(float distance) const {
    if (!isProximityChatEnabled()) return 1.0f;
    float fullDist = getProximityFullVolumeDistance();
    float maxDist = getProximityMaxDistance();
    if (distance <= fullDist) return 1.0f;
    if (distance >= maxDist) return 0.0f;
    float t = (distance - fullDist) / (maxDist - fullDist);
    float factor = 1.0f - t;
    return factor * factor;
}

void NetworkManager::setHearEnemiesVoice(bool hear) {
    hearEnemiesVoice.store(hear, std::memory_order_release);
    auto active = getBackend();
    if (active) {
        active->setHearEnemiesVoice(hear);
    }
}

bool NetworkManager::canHearEnemiesVoice() const {
    return hearEnemiesVoice.load(std::memory_order_acquire);
}

void NetworkManager::handleVoiceKeyDown() {
    VoiceChatMode mode = voiceMode.load(std::memory_order_acquire);
    if (mode == VOICE_MODE_OFF) return;
    if (mode == VOICE_MODE_PUSH_TO_TALK) {
        if (!isVoiceTransmitting()) {
            startVoiceTransmission();
        }
    } else if (mode == VOICE_MODE_TOGGLE) {
        if (isVoiceTransmitting()) {
            stopVoiceTransmission();
        } else {
            startVoiceTransmission();
        }
    }
}

void NetworkManager::handleVoiceKeyUp() {
    VoiceChatMode mode = voiceMode.load(std::memory_order_acquire);
    if (mode == VOICE_MODE_PUSH_TO_TALK) {
        if (isVoiceTransmitting()) {
            stopVoiceTransmission();
        }
    }
}

void NetworkManager::startVoiceTransmission() {
    auto active = getBackend();
    if (active) {
        active->startVoiceTransmission();
    }
}

void NetworkManager::stopVoiceTransmission() {
    auto active = getBackend();
    if (active) {
        active->stopVoiceTransmission();
    }
}

bool NetworkManager::isVoiceTransmitting() const {
    auto active = getBackend();
    return active ? active->isVoiceTransmitting() : false;
}

bool NetworkManager::isPlayerTalking(uint32_t playerId) const {
    auto active = getBackend();
    return active ? active->isPlayerTalking(playerId) : false;
}

void NetworkManager::setPlayerVoiceMuted(uint32_t playerId, bool muted) {
    auto active = getBackend();
    if (active) {
        active->setSpeakerMuted(playerId, muted);
    }
}

void NetworkManager::setPlayerVoiceVolume(uint32_t playerId, float volume) {
    auto active = getBackend();
    if (active) {
        active->setSpeakerVolume(playerId, volume);
    }
}

void NetworkManager::setMicrophoneVolume(int volume) {
    auto active = getBackend();
    if (active) {
        active->setMicrophoneVolume(volume);
    }
}

int NetworkManager::getMicrophoneVolume() const {
    auto active = getBackend();
    return active ? active->getMicrophoneVolume() : 100;
}

void NetworkManager::setVoicePlaybackVolume(int volume) {
    auto active = getBackend();
    if (active) {
        active->setVoicePlaybackVolume(volume);
    }
}

int NetworkManager::getVoicePlaybackVolume() const {
    auto active = getBackend();
    return active ? active->getVoicePlaybackVolume() : 100;
}

std::vector<std::string> NetworkManager::getCaptureDeviceNames() {
    auto active = getBackend();
    if (active) {
        return active->getCaptureDeviceNames();
    }
    gTeamVoice dummyVoice;
    return dummyVoice.getCaptureDeviceNames();
}

int NetworkManager::getCaptureDeviceIndex() const {
    auto active = getBackend();
    return active ? active->getCaptureDeviceIndex() : 0;
}

void NetworkManager::setCaptureDeviceIndex(int index) {
    auto active = getBackend();
    if (active) {
        active->setCaptureDeviceIndex(index);
    }
}

std::vector<std::string> NetworkManager::getPlaybackDeviceNames() {
    auto active = getBackend();
    if (active) {
        return active->getPlaybackDeviceNames();
    }
    gTeamVoice dummyVoice;
    return dummyVoice.getPlaybackDeviceNames();
}

int NetworkManager::getPlaybackDeviceIndex() const {
    auto active = getBackend();
    return active ? active->getPlaybackDeviceIndex() : 0;
}

void NetworkManager::setPlaybackDeviceIndex(int index) {
    auto active = getBackend();
    if (active) {
        active->setPlaybackDeviceIndex(index);
    }
}

std::string NetworkManager::getPlayerName(uint32_t netId) const {
    if (currentLobbyState) {
        for (size_t i = 0; i < currentLobbyState->playerIds.size(); ++i) {
            if (currentLobbyState->playerIds[i] == netId) {
                return currentLobbyState->playerNames[i];
            }
        }
    }
    auto active = getBackend();
    if (active) {
        for (const auto& rp : active->roomPlayers) {
            if (rp.id == netId) {
                return rp.name;
            }
        }
    }
    return "";
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
        if (p->success) {
            nm->onAuthSuccess(p->username, p->token);
            nm->setAuthResult(NetworkManager::AUTH_SUCCESS, p->message, p->username);
        } else {
            nm->setAuthResult(NetworkManager::AUTH_FAIL, p->message);
        }
    }
    void OnPacket(std::shared_ptr<gMasterUserRegisterResPacket> p) {
        if (p->success) {
            if (!p->token.empty()) {
                nm->onAuthSuccess("", p->token);
            }
            nm->setAuthResult(NetworkManager::AUTH_SUCCESS, p->message);
        } else {
            nm->setAuthResult(NetworkManager::AUTH_FAIL, p->message);
        }
    }
private:
    NetworkManager* nm;
};

void NetworkManager::onAuthSuccess(const std::string& username, const std::string& token) {
    std::lock_guard<std::mutex> lk(authMutex);
    if (!username.empty()) authUsername = username;
    if (!token.empty()) {
        sessionToken = token;
        if (!sessionEmail.empty()) {
            saveSession(sessionEmail, sessionToken);
        }
    }
}

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
    {
        std::lock_guard<std::mutex> lk(authMutex);
        sessionEmail = email;
    }
    runAuthRequest("Logging in...", [email, password]() {
        auto req = std::make_shared<gMasterUserLoginPacket>();
        req->email = email;
        req->password = password; // Sent over znet AES-256-GCM encrypted transport
        return req;
    });
}

void NetworkManager::loginWithToken(const std::string& email, const std::string& token) {
    {
        std::lock_guard<std::mutex> lk(authMutex);
        sessionEmail = email;
        sessionToken = token;
    }
    runAuthRequest("Logging in...", [email, token]() {
        auto req = std::make_shared<gMasterUserTokenLoginPacket>();
        req->email = email;
        req->token = token;
        return req;
    });
}

void NetworkManager::registerUser(const std::string& username, const std::string& email, const std::string& password) {
    {
        std::lock_guard<std::mutex> lk(authMutex);
        sessionEmail = email;
        authUsername = username;
    }
    runAuthRequest("Registering...", [username, email, password]() {
        auto req = std::make_shared<gMasterUserRegisterPacket>();
        req->username = username;
        req->email = email;
        req->password = password; // Sent over znet AES-256-GCM encrypted transport
        return req;
    });
}

void NetworkManager::logoutUser() {
    std::string email;
    std::string token;
    {
        std::lock_guard<std::mutex> lk(authMutex);
        email = sessionEmail;
        token = sessionToken;
        authUsername.clear();
        sessionEmail.clear();
        sessionToken.clear();
    }
    clearSession();
    clearAuthStatus();

    if (!token.empty()) {
        std::thread([email, token]() {
            auto client = std::make_shared<znet::Client>(znet::ClientConfig{MASTER_IP, MASTER_PORT, std::chrono::seconds(2), znet::ConnectionType::TCP});
            client->SetEventCallback([email, token](znet::Event& ev) {
                znet::EventDispatcher d{ev};
                d.Dispatch<znet::ClientConnectedToServerEvent>([email, token](znet::ClientConnectedToServerEvent& e) {
                    auto sess = e.session();
                    sess->SetCodec(makeAuthCodec());
                    auto req = std::make_shared<gMasterUserLogoutPacket>();
                    req->email = email;
                    req->token = token;
                    sess->SendPacket(req);
                    return false;
                });
            });
            client->Bind();
            client->Connect();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            client->Disconnect();
        }).detach();
    }
}

void NetworkManager::saveSession(const std::string& email, const std::string& sessionToken) {
    std::string payload = email + "\n" + sessionToken;
    std::vector<uint8_t> encrypted = encryptData(payload);

    std::ofstream file("saved_auth.dat", std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(encrypted.data()), encrypted.size());
        file.close();
    }
}

bool NetworkManager::loadSession(std::string& outEmail, std::string& outSessionToken) {
    std::ifstream file("saved_auth.dat", std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    if (size <= 16) return false;
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) return false;
    file.close();

    std::string decrypted = decryptData(buffer);
    if (decrypted.empty()) return false;

    size_t newline = decrypted.find('\n');
    if (newline == std::string::npos) return false;

    outEmail = decrypted.substr(0, newline);
    outSessionToken = decrypted.substr(newline + 1);
    return !outEmail.empty() && !outSessionToken.empty();
}

void NetworkManager::clearSession() {
    std::remove("saved_auth.dat");
}

bool NetworkManager::hasSavedSession() const {
    std::ifstream file("saved_auth.dat", std::ios::binary);
    return file.is_open();
}

void NetworkManager::autoLogin() {
    std::string email, token;
    if (loadSession(email, token)) {
        loginWithToken(email, token);
    }
}
