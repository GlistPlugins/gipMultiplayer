#include "GameBackend.h"

GameBackend::GameBackend() {
}

GameBackend::~GameBackend() {
}

void GameBackend::attachNode(uint32_t netid, gNode* node, bool local) {
	nodes[netid] = {node, local, -9999.f, -9999.f, -9999.f, 0.f, 0.f, 0.f};
}

void GameBackend::detachNode(uint32_t netid) {
	nodes.erase(netid);
}

void GameBackend::setOnJoin(std::function<void(uint32_t)> cb) {
	onjoin = std::move(cb);
}

void GameBackend::setOnLeave(std::function<void(uint32_t)> cb) {
	onleave = std::move(cb);
}

void GameBackend::enqueueState(uint32_t id, float x, float y, float z) {
	std::lock_guard<std::mutex> lock(queuemutex);
	queue.push_back({id, x, y, z, false});
}

void GameBackend::enqueueLeave(uint32_t id) {
	std::lock_guard<std::mutex> lk(queuemutex);
	queue.push_back({id, 0, 0, 0, true});
}

void GameBackend::update(float deltaTime) {
	// Drain the queue (thread-safe swap)
	std::vector<QueuedEvent> batch;
	{
		std::lock_guard<std::mutex> lock(queuemutex);
		batch.swap(queue);
	}

	for (auto& ev : batch) {
		if (ev.leave) {
			if (onleave) onleave(ev.id);
			continue;
		}

		// If this node ID hasn't been seen before, fire onJoin so the user
		// can create a visual and attachNode for it.
		auto it = nodes.find(ev.id);
		if (it == nodes.end()) {
			if (onjoin) onjoin(ev.id);
			it = nodes.find(ev.id);
			if (it == nodes.end()) continue;
		}

		// Set target position for remote nodes instead of snapping them instantly
		if (!it->second.local) {
			it->second.targetX = ev.x;
			it->second.targetY = ev.y;
			it->second.targetZ = ev.z;
		}
	}

	// Interpolate all remote nodes towards their target smoothly
	for (auto& kv : nodes) {
		if (!kv.second.local) {
			float curX = kv.second.node->getPosX();
			float curY = kv.second.node->getPosY();
			float curZ = kv.second.node->getPosZ();
			
			// Simple Lerp: start + (end - start) * factor
			float lerpFactor = 15.0f * deltaTime;
			if (lerpFactor > 1.0f) lerpFactor = 1.0f;
			
			kv.second.node->setPosition(
				curX + (kv.second.targetX - curX) * lerpFactor,
				curY + (kv.second.targetY - curY) * lerpFactor,
				curZ + (kv.second.targetZ - curZ) * lerpFactor
			);
		}
	}

	// Throttle broadcasts to a fixed network tick rate (e.g. 20 ticks per sec = 0.05f)
	networkTimer += deltaTime;
	if (networkTimer >= 0.05f) {
		networkTimer = 0.f;
		
		// Send each local node's current position to remote peers
		for (auto& kv : nodes) {
			if (!kv.second.local) continue;
			
			float cx = kv.second.node->getPosX();
			float cy = kv.second.node->getPosY();
			float cz = kv.second.node->getPosZ();
			
			// We MUST send the position even if it hasn't changed!
			// Due to a bug in the znet library where the client socket is accidentally blocking, 
			// the client's network thread will freeze on recv() and fail to send its own movements
			// UNLESS the host continuously sends packets to wake it up. This acts as a keep-alive.
			kv.second.lastX = cx;
			kv.second.lastY = cy;
			kv.second.lastZ = cz;
			broadcastState(kv.first, cx, cy, cz);
		}
	}
}
