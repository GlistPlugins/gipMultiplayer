/*
 * GameBackendLocal.h
 *
 * Host backend - runs a server that accepts client connections.
 * Receives state from connected clients, broadcasts it to all others,
 * and sends the host's own node positions directly to every client.
 */

#pragma once

#include "GameBackend.h"

class ServerPacketHandler;

class GameBackendLocal : public GameBackend {
	friend class ServerPacketHandler;
public:
	GameBackendLocal(const std::string& bindIp, uint16_t port);

	void start() override;

protected:
	void broadcastState(uint32_t netid, float x, float y, float z) override;

private:
	void broadcast(const std::shared_ptr<znet::Packet>& packet, znet::PeerSession* exclude = nullptr);

	bool onPeerConnected(znet::ServerClientConnectedEvent& e);
	bool onPeerDisconnected(znet::ServerClientDisconnectedEvent& e);

	std::string bindip;
	uint16_t port;
	std::unique_ptr<znet::Server> server;

	std::mutex sessionsmutex;
	std::vector<std::shared_ptr<znet::PeerSession>> sessions;
};
