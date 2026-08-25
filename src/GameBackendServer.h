#pragma once

#include "GameBackendLocal.h"

// A dedicated server backend that relays packets but does not participate as a player.
// It also communicates with the Master Server to list itself.
class GameBackendServer : public GameBackendLocal {
public:
    GameBackendServer(const std::string& bindIp, uint16_t port, const std::string& serverName, int teamSize, const std::string& masterIp = "127.0.0.1", uint16_t masterPort = 25010, const std::string& publicIp = "127.0.0.1", bool useP2P = false);

    void start() override;
};
