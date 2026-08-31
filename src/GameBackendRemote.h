/*
 * GameBackendRemote.h
 *
 * Client backend - connects to a remote server.
 * Sends local node positions to the server and receives state for
 * remote nodes from the server's broadcasts.
 */

#pragma once

#include "GameBackend.h"
#include "audio/gTeamVoice.h"
#include "znet/client.h"
#include "znet/client_events.h"

class GameBackendRemote : public GameBackend {
public:
	GameBackendRemote(const std::string& serverIp, uint16_t port);
	GameBackendRemote(std::shared_ptr<znet::PeerSession> existingSession);
	~GameBackendRemote() override;

	void start() override;
	void update(float deltaTime) override;
	void sendPacket(std::shared_ptr<znet::Packet> packet) override;

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

	void setMicrophoneVolume(int volume) override { voiceClient.setMicrophoneVolume(volume); }
	int getMicrophoneVolume() const override { return voiceClient.getMicrophoneVolume(); }
	void setVoicePlaybackVolume(int volume) override { voiceClient.setPlaybackVolume(volume); }
	int getVoicePlaybackVolume() const override { return voiceClient.getPlaybackVolume(); }

	std::vector<std::string> getCaptureDeviceNames() override { return voiceClient.getCaptureDeviceNames(); }
	int getCaptureDeviceIndex() const override { return voiceClient.getCaptureDeviceIndex(); }
	void setCaptureDeviceIndex(int index) override { voiceClient.setCaptureDeviceIndex(index); }

	std::vector<std::string> getPlaybackDeviceNames() override { return voiceClient.getPlaybackDeviceNames(); }
	int getPlaybackDeviceIndex() const override { return voiceClient.getPlaybackDeviceIndex(); }
	void setPlaybackDeviceIndex(int index) override { voiceClient.setPlaybackDeviceIndex(index); }

	void handleVoiceSessionPacket(const gTeamVoiceSessionPacket& p);
	void handleVoiceDownlinkPacket(const gTeamVoiceDownlinkPacket& p);

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

	gTeamVoice voiceClient;

	std::unique_ptr<znet::Client> client; // Declared last so it gets destroyed first
};
