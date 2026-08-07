#pragma once
#include "GameBackendLocal.h"
#include "znet/client.h"
#include <string>

class gDedicatedServer : public GameBackendLocal {
public:
    gDedicatedServer(const std::string& bindIp, uint16_t port);
    
    // Announce this server to a Master Server
    void enableMasterServerRegistration(const std::string& masterIp, uint16_t masterPort, const std::string& serverName, uint32_t maxPlayers);

    void tickMasterServer(float deltaTime);

private:
    bool useMasterServer = false;
    std::string masterIp;
    uint16_t masterPort;
    std::string serverName;
    uint32_t maxPlayers;
    
    float heartbeatTimer = 0.0f;
    std::shared_ptr<znet::Client> masterClient;
};
