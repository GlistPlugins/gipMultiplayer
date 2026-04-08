/*
* gipServerHandler.h
 *
 *  Created on: August 5, 2025
 *      Authors: Yusuf Ustaoglu, Muhammet Furkan Demir
 */

#ifndef SRC_GIPSERVERHANDLER_H_
#define SRC_GIPSERVERHANDLER_H_

#include "gipMultiplayer.h"
#include "gipPackets.h"

#include <algorithm>
#include <memory>
#include <string>

namespace gipMultiplayer = znet;

std::unique_ptr<Server> createGameServer(const std::string& bindIp, uint16_t port);

class gipServerHandler : public PacketHandler<gipServerHandler, PlayerStatePacket, PlayerDisconnectPacket>  {
public:
    explicit gipServerHandler(std::shared_ptr<PeerSession> s) : peerSessionPtr(std::move(s)) {}

    void OnPacket(std::shared_ptr<PlayerStatePacket> p);

    void OnUnknown(std::shared_ptr<Packet>);
private:
    std::shared_ptr<PeerSession> peerSessionPtr;
};

#endif /* SRC_GIPSERVERHANDLER_H_ */
