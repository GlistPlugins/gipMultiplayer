#include "GameBackend.h"

#include "znet/init.h"
#include "znet/logger.h"

static bool znet_initialized = false;
static int znet_instances = 0;

GameBackend::GameBackend() {
    if (!znet_initialized) {
        znet::Result result;
        if ((result = znet::Init()) != znet::Result::Success) {
            ZNET_LOG_ERROR("Failed to initialize znet");
        } else {
            znet_initialized = true;
        }
    }
    znet_instances++;
}

GameBackend::~GameBackend() {
    znet_instances--;
    if (znet_instances == 0 && znet_initialized) {
        znet::Cleanup();
        znet_initialized = false;
    }
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

void GameBackend::setOnTeamChanged(std::function<void(uint32_t, uint8_t)> cb) {
	onteamchanged = std::move(cb);
}

void GameBackend::setOnLeave(std::function<void(uint32_t)> cb) {
	onleave = std::move(cb);
}

void GameBackend::setOnConnected(std::function<void()> cb) {
	onconnected = std::move(cb);
}

void GameBackend::setOnDisconnected(std::function<void()> cb) {
	ondisconnected = std::move(cb);
}

void GameBackend::enqueueState(uint32_t id, float x, float y, float z, float yaw, uint8_t team) {
	std::lock_guard<std::mutex> lock(queuemutex);
	queue.push_back({id, x, y, z, yaw, team, false});
}

void GameBackend::enqueueLeave(uint32_t id) {
	std::lock_guard<std::mutex> lk(queuemutex);
	queue.push_back({id, 0, 0, 0, 0, 0, true});
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
				it->second.targetYaw = ev.yaw;
				if (it->second.team != ev.team) {
					it->second.team = ev.team;
					if (onteamchanged) onteamchanged(ev.id, ev.team);
				}
			}
	}

	// Send each local node's current position to remote peers
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
				float cyaw = kv.second.localYaw;

				// We MUST send the position even if it hasn't changed!
				// Due to a bug in the znet library where the client socket is accidentally blocking,
				// the client's network thread will freeze on recv() and fail to send its own movements
				// UNLESS the host continuously sends packets to wake it up. This acts as a keep-alive.
				kv.second.lastX = cx;
				kv.second.lastY = cy;
				kv.second.lastZ = cz;
				broadcastState(kv.first, cx, cy, cz, cyaw, localTeam);
			}
		}
}



void GameBackend::notifyConnected() {
	if (onconnected) onconnected();
}

void GameBackend::notifyDisconnected() {
	if (ondisconnected) ondisconnected();
}

void GameBackend::setLocalYaw(uint32_t netId, float yaw) {
	if(nodes.find(netId) != nodes.end()) {
		nodes[netId].localYaw = yaw;
	}
}

float GameBackend::getRemoteYaw(uint32_t netId) {
	if(nodes.find(netId) != nodes.end()) {
		return nodes[netId].targetYaw;
	}
	return 0;

}
