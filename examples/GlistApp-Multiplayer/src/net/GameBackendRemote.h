/*
 * GameBackendRemote.h
 *
 * Client backend - connects to a remote server.
 * Sends local node positions to the server and receives state for
 * remote nodes from the server's broadcasts.
 */

#pragma once

#include "GameBackend.h"

class GameBackendRemote : public GameBackend {
public:
	GameBackendRemote(const std::string& serverIp, uint16_t port);

	void start() override;

protected:
	void broadcastState(uint32_t netid, float x, float y, float z) override;

private:
	bool onConnected(znet::ClientConnectedToServerEvent& e);
	bool onDisconnected(znet::ClientDisconnectedFromServerEvent& e);

	std::string serverip;
	uint16_t port;
	std::unique_ptr<znet::Client> client;
	std::shared_ptr<znet::PeerSession> session;
};
