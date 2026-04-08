#include "GameBackend.h"

GameBackend::GameBackend() {
}

GameBackend::~GameBackend() {
}

void GameBackend::attachNode(uint32_t netid, gNode* node, bool local) {
	nodes[netid] = {node, local};
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

void GameBackend::update() {
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

		// Apply position to remote nodes
		if (!it->second.local) {
			it->second.node->setPosition(ev.x, ev.y, ev.z);
		}
	}

	// Send each local node's current position to remote peers
	for (auto& kv : nodes) {
		if (!kv.second.local) continue;
		// You might want to check if the position changed and then send it because this would be sending the same position every frame
		// even if it is not changed at all.
		// Additionally, you want to synchronize the orientation as well.
		broadcastState(kv.first, kv.second.node->getPosX(), kv.second.node->getPosY(), kv.second.node->getPosZ());
	}
}
