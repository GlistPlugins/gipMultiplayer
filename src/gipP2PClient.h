#pragma once

#include "znet/peer_session.h"
#include <string>
#include <memory>

class gipP2PClient {
public:
    gipP2PClient();
    ~gipP2PClient();

    // Connects to the master server, asks for roomCode/IP, waits for PunchExecute, runs PunchSync, and returns the session
    std::shared_ptr<znet::PeerSession> joinSession(const std::string& masterIp, uint16_t masterPort, const std::string& targetIdentifier, uint16_t localGamePort);
};
