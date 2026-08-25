#pragma once

#include "GameBackend.h"
#include <atomic>
#include <memory>
#include <string>

namespace znet {
    class Client;
}

class NetworkManager {
public:
    // Global access to the Network Manager
    static NetworkManager* getInstance();

    void disconnect();

    // Authentication
    void loginUser(const std::string& email, const std::string& password);
    void registerUser(const std::string& username, const std::string& email, const std::string& password);

    bool isHost() const { return hostMode; }

    // Lobby Actions
    void hostLobby(const std::string& playerName, const std::string& lobbyName, uint8_t teamSize, bool isPrivate = false, const std::string& password = "");
    void joinLobby(const std::string& ip, const std::string& playerName, const std::string& password = "", bool forceDirect = false);

    // Server browsing. Every result arrives through onServerQueried on the
    // main thread, whichever of the three started it.
    void clearQueries();
    void queryServer(const std::string& ip);
    void queryRoomCode(const std::string& roomCode);
    void refreshGlobalServers();

    uint8_t getLobbyTeamSize() const { return lobbyTeamSize; }
    void setLobbyTeamSize(uint8_t size) { lobbyTeamSize = size; }

    void toggleReady();
    void switchTeam(uint8_t teamId);
    void startMatch(); // Only works if isHost() is true
    void kickPlayer(uint32_t playerId);

    // Callbacks for UI
    void setOnServerQueried(std::function<void(std::string, std::string, std::string, std::string, std::string, bool, bool)> cb) { onServerQueried = cb; }
    void setOnLobbyStateUpdated(std::function<void(std::shared_ptr<LobbyStatePacket>)> cb) { onLobbyStateUpdated = cb; }
    void setOnMatchStarted(std::function<void()> cb) { onMatchStarted = cb; }
    void setOnDisconnected(std::function<void()> cb) { onDisconnected = cb; }
    void setOnKicked(std::function<void(std::string)> cb) { onKicked = cb; }

    std::function<void(std::string, std::string, std::string, std::string, std::string, bool, bool)> onServerQueried;
    std::function<void(std::shared_ptr<LobbyStatePacket>)> onLobbyStateUpdated;
    std::function<void()> onMatchStarted;
    std::function<void()> onDisconnected;
    std::function<void(std::string)> onKicked;

    // Installs a backend built elsewhere. The dedicated server entry point
    // uses this so its loop runs through update() like the game's does.
    void useBackend(std::shared_ptr<GameBackend> next);

    // The active backend, or null when not connected. Shared, because a join
    // running on another thread can swap it out at any moment: hold the handle
    // for as long as you use it rather than calling this twice.
    std::shared_ptr<GameBackend> getBackend() const;

    std::shared_ptr<LobbyStatePacket> currentLobbyState;

    // Call this from the game's main update loop to process network events
    void update(float deltaTime);

    std::string lobbyName;
    std::string currentRoomCode = "";

    // Auth State. The request runs on its own thread, so these go through
    // accessors rather than being touched directly.
    enum AuthStatus { AUTH_NONE, AUTH_PENDING, AUTH_SUCCESS, AUTH_FAIL };
    AuthStatus authStatus() const { return currentAuthStatus.load(std::memory_order_acquire); }
    std::string authMessage() const;
    std::string loggedInUsername() const;
    void clearAuthStatus();

    // Called by the packet handlers in NetworkManager.cpp, from network
    // threads. Both only queue, so the main thread is the one that acts.
    void pushQueryResult(const std::string& name, const std::string& format, const std::string& sizeStr,
                         const std::string& ip, const std::string& realIp, bool isDedicated, bool useP2P);
    void setAuthResult(AuthStatus status, const std::string& message, const std::string& username = "");

private:
    NetworkManager() = default;
    ~NetworkManager() = default;

    // Join attempts run on detached threads, so the backend is swapped from a
    // thread other than the one driving it. joinGeneration invalidates an
    // attempt the moment a newer one starts, so a slow attempt that finishes
    // late cannot install itself over the current backend.
    void setBackend(std::shared_ptr<GameBackend> next);
    uint64_t beginJoin();
    bool installBackend(std::shared_ptr<GameBackend> next, uint64_t generation);
    // Points a fresh backend's callbacks at ours.
    void wireBackend(const std::shared_ptr<GameBackend>& next);

    // Keeps a background browser client alive until clearQueries().
    void trackQueryClient(std::shared_ptr<znet::Client> client);
    // Login and registration are the same request/reply, so they share a body.
    void runAuthRequest(const std::string& pendingMessage, std::function<std::shared_ptr<znet::Packet>()> makeRequest);

    mutable std::mutex backendMutex;
    uint64_t joinGeneration = 0;
    std::shared_ptr<GameBackend> backend;
    bool wantsDisconnect = false;

    struct QueryResult {
        std::string name;
        std::string format;
        std::string sizeStr;
        std::string ip;
        std::string realIp;
        bool isDedicated;
        bool useP2P;
    };
    std::mutex queryMutex;
    std::vector<QueryResult> pendingQueries;
    std::vector<std::shared_ptr<znet::Client>> queryClients; // For background server browser pings

    std::atomic<AuthStatus> currentAuthStatus{AUTH_NONE};
    mutable std::mutex authMutex;
    std::string authMessageText;
    std::string authUsername;

    bool hostMode = false;
    uint8_t lobbyTeamSize = 2;
    std::string localPlayerName;
};
