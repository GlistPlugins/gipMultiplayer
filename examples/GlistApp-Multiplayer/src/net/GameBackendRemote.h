/*
 * GameBackendRemote.h
 *
 * Client backend - connects to a remote server.
 * Sends local node positions to the server and receives state for
 * remote nodes from the server's broadcasts.
 */

#pragma once

#include "GameBackend.h"

#include <atomic>
#include <thread>

class GameBackendRemote : public GameBackend {
public:
	GameBackendRemote(const std::string& serverIp, uint16_t port);
	~GameBackendRemote() override;

	void start() override;

protected:
	void broadcastState(uint32_t netid, float x, float y, float z) override;
	std::shared_ptr<znet::PeerSession> getVoiceSessionSnapshot() override;

private:
	bool onConnected(znet::ClientConnectedToServerEvent& e);
	bool onDisconnected(znet::ClientDisconnectedFromServerEvent& e);

	std::string serverip;
	uint16_t port;
	std::unique_ptr<znet::Client> client;
	std::thread connectionthread;
	mutable std::mutex sessionmutex;
	std::shared_ptr<znet::PeerSession> session;
	std::atomic<bool> readyqueued{false};
	std::atomic<std::int64_t> connectiondeadlinemilliseconds{0};
};
