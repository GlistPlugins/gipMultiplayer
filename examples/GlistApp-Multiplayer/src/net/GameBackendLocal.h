/*
 * GameBackendLocal.h
 *
 * Host backend - runs a server that accepts client connections.
 * Receives state from connected clients, broadcasts it to all others,
 * and sends the host's own node positions directly to every client.
 */

#pragma once

#include "GameBackend.h"

#include <chrono>
#include <unordered_set>
#include <thread>

class ServerPacketHandler;
class LocalVoicePacketHandler;

class GameBackendLocal : public GameBackend {
	friend class ServerPacketHandler;
	friend class LocalVoicePacketHandler;
public:
	GameBackendLocal(const std::string& bindIp, uint16_t port);
	~GameBackendLocal() override;

	void start() override;

protected:
	void broadcastState(uint32_t netid, float x, float y, float z) override;
	std::shared_ptr<znet::PeerSession> getVoiceSessionSnapshot() override;

private:
	void broadcast(const std::shared_ptr<znet::Packet>& packet, znet::PeerSession* exclude = nullptr);
	void handleVoiceUplink(gTeamVoiceServer::ConnectionId connectionid, const gTeamVoiceUplinkPacket& packet);
	void authorizeVoicePeer(gTeamVoiceServer::ConnectionId connectionid);
	void retryVoiceAuthorizations();

	bool onPeerConnected(znet::ServerClientConnectedEvent& e);
	bool onPeerDisconnected(znet::ServerClientDisconnectedEvent& e);
	bool onLocalVoiceConnected(znet::ClientConnectedToServerEvent& e);
	bool onLocalVoiceDisconnected(znet::ClientDisconnectedFromServerEvent& e);

	std::string bindip;
	uint16_t port;
	std::unique_ptr<znet::Server> server;
	std::unique_ptr<znet::Client> localvoiceclient;
	std::thread localvoiceconnectionthread;
	gTeamVoiceServer voicerouter;
	std::uint64_t voicesessionid;

	std::mutex sessionsmutex;
	std::vector<std::shared_ptr<znet::PeerSession>> sessions;
	std::mutex localvoicesessionmutex;
	std::shared_ptr<znet::PeerSession> localvoicesession;
	std::atomic<bool> localreadyqueued{false};
	std::atomic<std::int64_t> localconnectiondeadlinemilliseconds{0};
	std::mutex pendingvoicemutex;
	std::unordered_set<gTeamVoiceServer::ConnectionId> connectedvoicepeers;
	std::unordered_set<gTeamVoiceServer::ConnectionId> pendingvoicepeers;
	std::chrono::steady_clock::time_point nextvoiceretry{};
};
