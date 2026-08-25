/*
 * GameBackendRemote.h
 *
 * Client backend - connects to a remote server.
 * Sends local node positions to the server and receives state for
 * remote nodes from the server's broadcasts.
 */

#pragma once

#include "GameBackend.h"
#include "znet/client.h"
#include "znet/client_events.h"

class GameBackendRemote : public GameBackend {
public:
	GameBackendRemote(const std::string& serverIp, uint16_t port);
	GameBackendRemote(std::shared_ptr<znet::PeerSession> existingSession);
	~GameBackendRemote() override;

	void start() override;
	void sendPacket(std::shared_ptr<znet::Packet> packet) override;

protected:
	void broadcastState(uint32_t netid, float x, float y, float z, float yaw, uint8_t team, uint8_t animState) override;
	void broadcastLobbyState() override;

	void broadcastFireEvent(uint32_t shooterId, uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz) override;
	void broadcastHitEvent(uint32_t attackerId, uint32_t victimId, float damage) override;
	void broadcastKillEvent(uint32_t killerId, uint32_t victimId) override;

private:
	// Packets sent before there is a session are held here; a peer that never
	// connects must not be able to grow this without end.
	static constexpr size_t MAX_PENDING_PACKETS = 256;

	void adoptSession(const std::shared_ptr<znet::PeerSession>& session);
	bool onConnected(znet::ClientConnectedToServerEvent& e);
	bool onDisconnected(znet::ClientDisconnectedFromServerEvent& e);

	std::string serverip;
	uint16_t port;
	
	std::mutex sessionmutex;
	std::shared_ptr<znet::PeerSession> session;
	std::vector<std::shared_ptr<znet::Packet>> pendingPackets;

	std::unique_ptr<znet::Client> client; // Declared last so it gets destroyed first
};
