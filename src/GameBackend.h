/*
 * GameBackend.h
 *
 * Abstract base class for network replication. Manages a registry of gNode
 * pointers tagged as local (we send their position) or remote (we receive
 * and apply their position). Subclasses implement the transport layer.
 *
 * Threading model:
 *   - attachNode/detachNode/update run on the main (render) thread.
 *   - enqueueState/enqueueLeave are called from the network thread by
 *     packet handlers, and are protected by queuemutex.
 *   - update() drains the queue on the main thread, so node mutations
 *     only happen on the main thread.
 */

#pragma once

#include "GamePackets.h"
#include "gNode.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

class GameBackend {
public:
	virtual ~GameBackend();

	// Called once after construction to start listening or connecting
	virtual void start() = 0;
	
	// Send a packet over the network
	virtual void sendPacket(std::shared_ptr<znet::Packet> packet) = 0;

	// Disconnects a specific player if this backend is the host
	virtual void kickPlayer(uint32_t playerId) {}

	// Request to start the match (Host only)
	virtual void startMatch() {}

	// Register a gNode for network syncing. The registry shares ownership, so
	// the node stays alive for as long as it is attached.
	// local=true: this node's position is sent to remote peers each frame.
	// local=false: this node's position is updated from incoming network data.
	void attachNode(uint32_t netId, std::shared_ptr<gNode> node, bool local);
	void detachNode(uint32_t netId);

	//yaw
	void setLocalYaw(uint32_t netId, float yaw);
	float getRemoteYaw(uint32_t netId);

	//animation
	void setLocalAnimState(uint32_t netId, uint8_t animState);
	uint8_t getRemoteAnimState(uint32_t netId);


	void sendFireEvent(uint32_t shooterId, uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz);
	void sendHitEvent(uint32_t attackerId, uint32_t victimId, float damage);
	void sendKillEvent(uint32_t killerId, uint32_t victimId);

	//Callbacks
	void setOnPlayerFired(std::function<void(uint32_t, uint8_t, float, float, float, float, float, float)> cb);
	void setOnPlayerHit(std::function<void(uint32_t, uint32_t, float)> cb);
	void setOnPlayerKilled(std::function<void(uint32_t, uint32_t)> cb);

	// Call once per frame. Processes incoming state from the network thread
	// and sends local node positions via broadcastState().
	virtual void update(float deltaTime);

	virtual bool isServer() const { return false; }

	// Lobby functions fired during update() when a new remote node appears or leaves.
	// Use onJoin to create a visual and call attachNode for the new ID.
	// Use onLeave to call detachNode and clean up the visual.
	void setOnJoin(std::function<void(uint32_t)> cb);
	void setOnLeave(std::function<void(uint32_t)> cb);
	void setOnTeamChanged(std::function<void(uint32_t, uint8_t)> cb);
	
	// Callbacks for connection state
	void setOnConnected(std::function<void()> cb);
	void setOnDisconnected(std::function<void()> cb);

	// Implemented by GameBackend to handle incoming packets safely on the main thread.
	void onPacketReceived(std::shared_ptr<znet::Packet> packet);

	// Enqueue a packet safely from a background network thread. (this will be removed when safe multithread support added to znet)
	void enqueuePacket(std::shared_ptr<znet::Packet> packet);

	// Defers work from a network thread to the next update(). Anything that
	// touches game or UI state has to go through here.
	void runOnMainThread(std::function<void()> task);
	
	void notifyConnected();
	void notifyDisconnected();

	// Lobby callbacks
	void setOnLobbyStateUpdated(std::function<void(std::shared_ptr<LobbyStatePacket>)> cb) { onLobbyStateUpdated = cb; }
	void setOnMatchStarted(std::function<void()> cb) { onMatchStarted = cb; }
	void setOnKicked(std::function<void(std::string)> cb) { onKicked = cb; }

	std::function<void(std::shared_ptr<LobbyStatePacket>)> onLobbyStateUpdated;
	std::function<void()> onMatchStarted;

	struct RoomPlayerState {
		uint32_t id;
		std::string name;
		uint8_t team;
		bool isReady;
	};
	// Main thread only. Off it, read playerCount() instead.
	std::vector<RoomPlayerState> roomPlayers;
	virtual void broadcastLobbyState() = 0;

	// roomPlayers.size(), safe to read from a network thread.
	uint32_t playerCount() const { return playerCountCache.load(std::memory_order_relaxed); }

	void setLocalTeam(uint8_t teamId) { localTeam = teamId; }
	uint8_t getLocalTeam() const { return localTeam; }

	int getPing() const { return currentPing.load(std::memory_order_relaxed); }
	void onPongReceived(uint64_t timestamp);

	// Voice Chat Interface
	virtual bool initializeVoice() { return false; }
	virtual void shutdownVoice() {}
	virtual void startVoiceTransmission() {}
	virtual void stopVoiceTransmission() {}
	virtual bool isVoiceTransmitting() const { return false; }
	virtual bool isPlayerTalking(uint32_t playerId) const { return false; }
	virtual void setSpeakerMuted(uint32_t playerId, bool muted) {}
	virtual void setSpeakerVolume(uint32_t playerId, float volume) {}
	virtual void setVoiceEnabled(bool enabled) {}
	virtual bool isVoiceEnabled() const { return true; }
	virtual void setHearEnemiesVoice(bool hear) {}
	virtual bool canHearEnemiesVoice() const { return false; }

	virtual void setMicrophoneVolume(int volume) {}
	virtual int getMicrophoneVolume() const { return 100; }
	virtual void setVoicePlaybackVolume(int volume) {}
	virtual int getVoicePlaybackVolume() const { return 100; }

	virtual std::vector<std::string> getCaptureDeviceNames() { return {"Varsayilan"}; }
	virtual int getCaptureDeviceIndex() const { return 0; }
	virtual void setCaptureDeviceIndex(int index) {}

	virtual std::vector<std::string> getPlaybackDeviceNames() { return {"Varsayilan"}; }
	virtual int getPlaybackDeviceIndex() const { return 0; }
	virtual void setPlaybackDeviceIndex(int index) {}

protected:
	GameBackend();

	// Subclasses implement this to send a node's position to remote peers.
	// Called by update() for each local node.
	virtual void broadcastState(uint32_t netId, float x, float y, float z, float yaw, uint8_t team, uint8_t animState) = 0;
	
	uint8_t localTeam = 1;

	virtual void broadcastFireEvent(uint32_t shooterId, uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz) = 0;
	virtual void broadcastHitEvent(uint32_t attackerId, uint32_t victimId, float damage) = 0;
	virtual void broadcastKillEvent(uint32_t killerId, uint32_t victimId) = 0;

protected:
	std::mutex queueMutex;
	std::vector<std::shared_ptr<znet::Packet>> packetQueue;
	std::vector<std::function<void()>> mainThreadTasks;

	struct NetNode {
		std::shared_ptr<gNode> node;
		bool local = false;
		uint8_t team = 0;
		float targetX = 0.f, targetY = 0.f, targetZ = 0.f; // Interpolation targets
		float targetYaw = 0.0f;
		float localYaw = 0.0f;
		uint8_t targetAnimState = 0;
		uint8_t localAnimState = 0;
	};
	std::unordered_map<uint32_t, NetNode> nodes;

	// Kept in step with roomPlayers by publishPlayerCount().
	std::atomic<uint32_t> playerCountCache{0};
	void publishPlayerCount() { playerCountCache.store(static_cast<uint32_t>(roomPlayers.size()), std::memory_order_relaxed); }

	float networkTimer = 0.f;
	float timeSinceLastKeepAlive = 0.f;
	float keepAliveTimer = 0.f;
	float pingTimer = 0.f;
	std::atomic<int> currentPing{0};
	bool disconnectNotified = false;
	bool isDedicatedServer = false;

	std::function<void(uint32_t)> onjoin;
	std::function<void(uint32_t)> onleave;
	std::function<void(uint32_t, uint8_t)> onteamchanged;

	std::function<void()> onconnected;
	std::function<void()> ondisconnected;
	std::function<void(std::string)> onKicked;

	std::function<void(uint32_t, uint8_t, float, float, float, float, float, float)> onplayerfired;
	std::function<void(uint32_t, uint32_t, float)> onplayerhit;
	std::function<void(uint32_t, uint32_t)> onplayerkilled;
};
