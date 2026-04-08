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

	// Call once per frame. Processes incoming state from the network thread
	// and sends local node positions via broadcastState().
	void update();

	// Callbacks fired during update() when a new remote node appears or leaves.
	// Use onJoin to create a visual and call attachNode for the new ID.
	// Use onLeave to call detachNode and clean up the visual.
	void setOnJoin(std::function<void(uint32_t)> cb);
	void setOnLeave(std::function<void(uint32_t)> cb);

	// Called from the network thread by packet handlers to queue incoming data.
	// Thread-safe, protected by queuemutex.
	void enqueueState(uint32_t id, float x, float y, float z);
	void enqueueLeave(uint32_t id);

protected:
	GameBackend();

	// Subclasses implement this to send a node's position to remote peers.
	// Called by update() for each local node.
	virtual void broadcastState(uint32_t netId, float x, float y, float z) = 0;

private:
	struct NetNode {
		gNode* node;
		bool local;
	};
	std::unordered_map<uint32_t, NetNode> nodes;

	struct QueuedEvent {
		uint32_t id;
		float x, y, z;
		bool leave;
	};
	std::mutex queuemutex;
	std::vector<QueuedEvent> queue;

	std::function<void(uint32_t)> onjoin;
	std::function<void(uint32_t)> onleave;
};
