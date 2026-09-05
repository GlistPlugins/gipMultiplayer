#include "GameBackendServer.h"
#include "NetworkManager.h"
#include <iostream>

GameBackendServer::GameBackendServer(const std::string& bindIp, uint16_t port, const std::string& serverName, int teamSize, const std::string& masterIp, uint16_t masterPort, const std::string& publicIp, bool useP2P, uint16_t masterRelayPort, std::shared_ptr<znet::InetAddress> extraReflector)
    : GameBackendLocal(bindIp, port) {
    this->serverName = serverName;
    this->targetMasterIp = masterIp;
    this->targetMasterPort = masterPort;
    this->targetMasterRelayPort = masterRelayPort;
    this->targetExtraReflector = std::move(extraReflector);
    this->publicIp = publicIp;
    this->isDedicatedServer = true;
    this->useP2P = useP2P;
    NetworkManager::getInstance()->setLobbyTeamSize(teamSize);
}

void GameBackendServer::start() {
    GameBackendLocal::start();
    std::cout << "[DedicatedServer] Listening on port " << port << std::endl;
    // Dedicated servers have no password by default. A second reflector is
    // passed only when one behind NAT was configured with it.
    registerWithMasterServer(serverName, false, "", targetMasterIp, targetMasterPort, targetMasterRelayPort, targetExtraReflector, publicIp, useP2P);
}
