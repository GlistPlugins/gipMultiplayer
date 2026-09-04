#include "GameBackendServer.h"
#include "NetworkManager.h"
#include <iostream>

GameBackendServer::GameBackendServer(const std::string& bindIp, uint16_t port, const std::string& serverName, int teamSize, const std::string& masterIp, uint16_t masterPort, const std::string& publicIp, bool useP2P, uint16_t masterRelayPort)
    : GameBackendLocal(bindIp, port) {
    this->serverName = serverName;
    this->targetMasterIp = masterIp;
    this->targetMasterPort = masterPort;
    this->targetMasterRelayPort = masterRelayPort;
    this->publicIp = publicIp;
    this->isDedicatedServer = true;
    this->useP2P = useP2P;
    NetworkManager::getInstance()->setLobbyTeamSize(teamSize);
}

void GameBackendServer::start() {
    GameBackendLocal::start();
    std::cout << "[DedicatedServer] Listening on port " << port << std::endl;
    // Dedicated servers are public and have no password by default.
    registerWithMasterServer(serverName, false, "", targetMasterIp, targetMasterPort, targetMasterRelayPort, publicIp, useP2P);
}
