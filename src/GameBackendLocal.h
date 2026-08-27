/*
 * GameBackendLocal.h
 *
 * Host backend - runs a server that accepts client connections.
 * Receives state from connected clients, broadcasts it to all others,
 * and sends the host's own node positions directly to every client.
 */

#pragma once

#include "GameBackend.h"
#include "audio/gTeamVoice.h"
#include "voice/gTeamVoiceServer.h"
#include "master/gMasterPackets.h"
#include "znet/p2p.h"

class ServerPacketHandler;

class GameBackendLocal : public GameBackend {
	friend class ServerPacketHandler;
public:
	GameBackendLocal(const std::string& bindIp, uint16_t port);
	~GameBackendLocal() override;

	void start() override;
	bool isServer() const override { return true; }
	void sendPacket(std::shared_ptr<znet::Packet> packet) override;
	void kickPlayer(uint32_t playerId) override;

	// Voice Chat Interface
	bool initializeVoice() override;
	void shutdownVoice() override;
	void startVoiceTransmission() override;
	void stopVoiceTransmission() override;
	bool isVoiceTransmitting() const override;
	bool isPlayerTalking(uint32_t playerId) const override;
	void setSpeakerMuted(uint32_t playerId, bool muted) override;
	void setSpeakerVolume(uint32_t playerId, float volume) override;
	void setVoiceEnabled(bool enabled) override;
	bool isVoiceEnabled() const override;
	void setHearEnemiesVoice(bool hear) override;
	bool canHearEnemiesVoice() const override;

	void handleVoiceUplinkPacket(gTeamVoiceServer::ConnectionId connId, const gTeamVoiceUplinkPacket& p);
	void handleVoiceSessionPacket(const gTeamVoiceSessionPacket& p);
	void handleVoiceDownlinkPacket(const gTeamVoiceDownlinkPacket& p);
	void syncVoicePeerStates();

protected:
	void broadcastState(uint32_t netid, float x, float y, float z, float yaw, uint8_t team, uint8_t animState) override;
	void broadcastLobbyState() override;

	void broadcastFireEvent(uint32_t shooterId, uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz) override;
	void broadcastHitEvent(uint32_t attackerId, uint32_t victimId, float damage) override;
	void broadcastKillEvent(uint32_t killerId, uint32_t victimId) override;

protected:
	void broadcast(const std::shared_ptr<znet::Packet>& packet, znet::PeerSession* exclude = nullptr);

	bool onPeerConnected(znet::IncomingClientConnectedEvent& e);
	bool onPeerDisconnected(znet::IncomingClientDisconnectedEvent& e);

	std::string bindip;
	uint16_t port;

	std::mutex sessionsmutex;
	std::vector<std::shared_ptr<znet::PeerSession>> sessions;
	
	std::unique_ptr<znet::Server> server; // Declared last so it gets destroyed first
	std::unique_ptr<znet::Server> queryServer;

public:
	void registerWithMasterServer(const std::string& name, bool isPrivate, const std::string& password, const std::string& masterIp, uint16_t masterPort, const std::string& publicIp, bool useP2P = false);
	void update(float deltaTime) override;

	// The code the master assigned this lobby, empty until it answers.
	std::string roomCode() const;

protected:
	std::string serverName;
	bool isPrivateServer = false;
	std::string serverPassword = "";
	std::string targetMasterIp = "127.0.0.1";
	uint16_t targetMasterPort = 25010;
	// Both the register and the heartbeat send one, so it is built in one place.
	std::shared_ptr<gMasterRegisterPacket> makeRegisterPacket() const;
	// Port peers are told to use; punching runs on a separate socket.
	uint16_t advertisedPort() const;
	// Advertised "host:port", and every local network at that same port.
	std::string advertisedAddress() const;
	std::vector<std::string> localAddresses() const;

	// Wires a session with the codec and handler the accept path uses, whether
	// it arrived from the listener or from a punch.
	void adoptSession(const std::shared_ptr<znet::PeerSession>& session);

	// One socket for every punched player. A NAT hands out one mapping per
	// socket, so punching each peer from its own socket makes their packets
	// arrive on the wrong session and fail authentication.
	std::unique_ptr<znet::p2p::Host> punchHost;
	// Brings the punch host up on first use, since only P2P servers need it.
	znet::p2p::Host* ensurePunchHost();
	void onPunchResolved(znet::Result result, std::shared_ptr<znet::PeerSession> session);

	std::string publicIp = "127.0.0.1";
	bool useP2P = false;
	
	std::unique_ptr<znet::Client> masterClient;
	float masterHeartbeatTimer = 0.0f;
	// Flipped by the master link's thread, read by update() on the main one.
	std::atomic<bool> isConnectedToMaster{false};

	mutable std::mutex roomCodeMutex;
	std::string assignedRoomCode;

	// Voice Chat Subsystems
	gTeamVoiceServer voiceRouter;
	gTeamVoice voiceClient;
	uint64_t voiceSessionId = 0;
	bool isDedicatedServer = false;
};
