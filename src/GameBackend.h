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

#include "gipMultiplayer.h"
#include "GamePackets.h"
#include "gNode.h"
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

	// Register a gNode for network syncing.
	// local=true: this node's position is sent to remote peers each frame.
	// local=false: this node's position is updated from incoming network data.
	void attachNode(uint32_t netId, gNode* node, bool local);
	void detachNode(uint32_t netId);

	//yaw
	void setLocalYaw(uint32_t netId, float yaw);
	float getRemoteYaw(uint32_t netId);

	// Call once per frame. Processes incoming state from the network thread
	// and sends local node positions via broadcastState().
	void update(float deltaTime);

	// Callbacks fired during update() when a new remote node appears or leaves.
	// Use onJoin to create a visual and call attachNode for the new ID.
	// Use onLeave to call detachNode and clean up the visual.
	void setOnJoin(std::function<void(uint32_t)> cb);
	void setOnLeave(std::function<void(uint32_t)> cb);
	void setOnTeamChanged(std::function<void(uint32_t, uint8_t)> cb);
	void setOnConnected(std::function<void()> cb);
	void setOnDisconnected(std::function<void()> cb);

	// Called from the network thread by packet handlers to queue incoming data.
	// Thread-safe, protected by queuemutex.
	void enqueueState(uint32_t id, float x, float y, float z, float yaw, uint8_t team);
	void enqueueLeave(uint32_t id);

	void setLocalTeam(uint8_t teamId) { localTeam = teamId; }
	uint8_t getLocalTeam() const { return localTeam; }

protected:
	GameBackend();

	// Subclasses implement this to send a node's position to remote peers.
	// Called by update() for each local node.
	virtual void broadcastState(uint32_t netId, float x, float y, float z, float yaw, uint8_t team) = 0;
	
	uint8_t localTeam = 1;
	void notifyConnected();
	void notifyDisconnected();

private:
	struct NetNode {
		gNode* node;
		bool local;
		uint8_t team = 0;
		float lastX = -9999.f, lastY = -9999.f, lastZ = -9999.f;
		float targetX = 0.f, targetY = 0.f, targetZ = 0.f; // Interpolation targets
		float targetYaw = 0.0f;
		float localYaw = 0.0f;

	};
	std::unordered_map<uint32_t, NetNode> nodes;

	float networkTimer = 0.f;

	struct QueuedEvent {
		uint32_t id;
		float x, y, z;
		float yaw;
		uint8_t team;
		bool leave;
	};
	std::mutex queuemutex;
	std::vector<QueuedEvent> queue;

	std::function<void(uint32_t)> onjoin;
	std::function<void(uint32_t)> onleave;
	std::function<void(uint32_t, uint8_t)> onteamchanged;
	std::function<void()> onconnected;
	std::function<void()> ondisconnected;
};
