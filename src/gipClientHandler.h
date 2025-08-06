/*
* gipClientHandler.h
 *
 *  Created on: August 5, 2025
 *      Created by: Yusuf Ustaoglu
 */

#ifndef GIPCLIENTHANDLER_H
#define GIPCLIENTHANDLER_H

#include "gipPackets.h"

namespace gipMultiplayer = znet;

std::unique_ptr<gipMultiplayer::Client> createGameClient(const std::string& serverIp, uint16_t port);
void setRemoteStateCallback(const std::function<void(uint32_t, float, float, float)>& cb);
void setPlayerDisconnectCallback(const std::function<void(uint32_t)>& cb);
void setServerDisconnectedCallback(const std::function<void()>& cb);
bool sendLocalState(uint32_t id, float x, float y, float z);

class gipClientHandler : public PacketHandler<gipClientHandler, PlayerStatePacket, PlayerDisconnectPacket> {
public:
	explicit gipClientHandler(std::shared_ptr<PeerSession> s) : peerSessionPtr(std::move(s)) {}

	void OnPacket(std::shared_ptr<PlayerStatePacket> p);
	void OnPacket(std::shared_ptr<PlayerDisconnectPacket> p);
	void OnUnknown(std::shared_ptr<Packet>);
private:
	std::shared_ptr<PeerSession> peerSessionPtr;
};

#endif //GIPCLIENTHANDLER_H