#include "GameBackend.h"
#include "NetworkManager.h"
#include <chrono>

GameBackend::GameBackend() {
}

GameBackend::~GameBackend() {
}


void GameBackend::enqueuePacket(std::shared_ptr<znet::Packet> packet) {
	std::lock_guard<std::mutex> lock(queueMutex);
	packetQueue.push_back(std::move(packet));
}

void GameBackend::runOnMainThread(std::function<void()> task) {
	std::lock_guard<std::mutex> lock(queueMutex);
	mainThreadTasks.push_back(std::move(task));
}

void GameBackend::setOnConnected(std::function<void()> cb) {
	onconnected = std::move(cb);
}

void GameBackend::setOnDisconnected(std::function<void()> cb) {
	ondisconnected = std::move(cb);
}

void GameBackend::notifyConnected() {
	if (onconnected) onconnected();
}

void GameBackend::notifyDisconnected() {
	if (ondisconnected) ondisconnected();
}

void GameBackend::attachNode(uint32_t netid, std::shared_ptr<gNode> node, bool local) {
	if (!node) return;
	NetNode entry;
	entry.local = local;
	// Without this a remote node lerps in from the origin until its first state packet.
	entry.targetX = node->getPosX();
	entry.targetY = node->getPosY();
	entry.targetZ = node->getPosZ();
	entry.node = std::move(node);
	nodes[netid] = std::move(entry);
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

// Handled by main thread via gipNetworkBackend::update(deltaTime)
void GameBackend::onPacketReceived(std::shared_ptr<znet::Packet> packet) {
	if (packet->id() == PACKET_KEEPALIVE) {
		timeSinceLastKeepAlive = 0.0f;
		return;
	}

	if (packet->id() == PACKET_NODE_LEAVE) {
		auto p = std::static_pointer_cast<NodeLeavePacket>(packet);
		if (onleave) onleave(p->netid);
		
		// Remove from lobby if present
		for (auto it = roomPlayers.begin(); it != roomPlayers.end(); ++it) {
			if (it->id == p->netid) {
				roomPlayers.erase(it);
				publishPlayerCount();
				broadcastLobbyState();
				break;
			}
		}
		return;
	}
	
	if (packet->id() == PACKET_NODE_FIRE) {
		auto p = std::static_pointer_cast<PlayerFirePacket>(packet);
		if (onplayerfired) onplayerfired(p->shooterId, p->gunType, p->originX, p->originY, p->originZ, p->dirX, p->dirY, p->dirZ);
		return;
	}

	if (packet->id() == PACKET_NODE_HIT) {
		auto p = std::static_pointer_cast<PlayerHitPacket>(packet);
		if (onplayerhit) onplayerhit(p->attackerId, p->victimId, p->damage);
		return;
	}

	if (packet->id() == PACKET_NODE_KILLED) {
		auto p = std::static_pointer_cast<PlayerKilledPacket>(packet);
		if (onplayerkilled) onplayerkilled(p->killerId, p->victimId);
		return;
	}

	if (packet->id() == PACKET_NODE_STATE) {
		auto ev = std::static_pointer_cast<NodeStatePacket>(packet);

		// If this node ID hasn't been seen before, fire onJoin so the user
		// can create a visual and attachNode for it.
		auto it = nodes.find(ev->netid);
		if (it == nodes.end()) {
			if (onjoin) onjoin(ev->netid);
			it = nodes.find(ev->netid);
			if (it == nodes.end()) return;
		}

		// Set target position for remote nodes instead of snapping them instantly
		if (!it->second.local) {
			it->second.targetX = ev->x;
			it->second.targetY = ev->y;
			it->second.targetZ = ev->z;
			it->second.targetYaw = ev->yaw;
			it->second.targetAnimState = ev->animState;
			if (it->second.team != ev->team) {
				it->second.team = ev->team;
				if (onteamchanged) onteamchanged(ev->netid, ev->team);
			}
		}
		return;
	}

	if (packet->id() == PACKET_LOBBY_STATE) {
		auto p = std::static_pointer_cast<LobbyStatePacket>(packet);
		roomPlayers.clear();
		for (size_t i = 0; i < p->playerIds.size(); i++) {
			roomPlayers.push_back({p->playerIds[i], p->playerNames[i], p->playerTeams[i], p->playerReadys[i] != 0});
		}
		publishPlayerCount();
		NetworkManager::getInstance()->currentRoomCode = p->roomCode;
		if (onLobbyStateUpdated) onLobbyStateUpdated(p);
		return;
	}

	if (packet->id() == PACKET_START_MATCH) {
		if (onMatchStarted) onMatchStarted();
		return;
	}

	if (packet->id() == PACKET_LOBBY_KICK) {
		auto p = std::static_pointer_cast<LobbyKickPacket>(packet);
		if (onKicked) onKicked(p->reason);
		return;
	}

	// The following packets are ONLY handled by the Host.
	if (packet->id() == PACKET_LOBBY_JOIN) {
		auto p = std::static_pointer_cast<LobbyJoinPacket>(packet);
		std::cout << "[GameBackend] Processing LOBBY_JOIN for ID: " << p->senderId << " Name: " << p->playerName << std::endl;
		
		// A join is resent until the lobby lists the sender, so the same one
		// arriving twice has to mean the same player, not a second copy.
		for (const auto& rp : roomPlayers) {
			if (rp.id == p->senderId) {
				broadcastLobbyState();
				return;
			}
		}

		size_t maxSize = static_cast<size_t>(NetworkManager::getInstance()->getLobbyTeamSize()) * 2;
		if (roomPlayers.size() >= maxSize) {
			std::cout << "[GameBackend] Rejecting join, room is full!" << std::endl;
			return; // Reject join if room is full
		}
		
		// Auto-balance team
		int redCount = 0; int blueCount = 0;
		for (auto& rp : roomPlayers) { if (rp.team == 1) redCount++; else if (rp.team == 2) blueCount++; }
		uint8_t team = (redCount <= blueCount) ? 1 : 2;
		
		// Resolve duplicate names
		std::string finalName = p->playerName;
		int duplicateCount = 1;
		auto nameExists = [&](const std::string& n) {
			for (const auto& rp : roomPlayers) {
				if (rp.name == n) return true;
			}
			return false;
		};
		while (nameExists(finalName)) {
			finalName = p->playerName + " " + std::to_string(duplicateCount);
			duplicateCount++;
		}
		
		std::cout << "[GameBackend] Added player " << p->senderId << " to roomPlayers as " << finalName << std::endl;
		roomPlayers.push_back({p->senderId, finalName, team, false});
		publishPlayerCount();
		broadcastLobbyState();
		return;
	}

	if (packet->id() == PACKET_TOGGLE_READY) {
		auto p = std::static_pointer_cast<ToggleReadyPacket>(packet);
		for (auto& rp : roomPlayers) {
			if (rp.id == p->senderId) {
				rp.isReady = !rp.isReady;
				break;
			}
		}
		broadcastLobbyState();
		return;
	}

	if (packet->id() == PACKET_SWITCH_TEAM) {
		auto p = std::static_pointer_cast<SwitchTeamPacket>(packet);
		for (auto& rp : roomPlayers) {
			if (rp.id == p->senderId) {
				rp.team = p->teamId;
				break;
			}
		}
		broadcastLobbyState();
		return;
	}
}



void GameBackend::update(float deltaTime) {
	std::vector<std::shared_ptr<znet::Packet>> batch;
	std::vector<std::function<void()>> tasks;
	{
		std::lock_guard<std::mutex> lock(queueMutex);
		batch.swap(packetQueue);
		tasks.swap(mainThreadTasks);
	}

	for (const auto& p : batch) {
		onPacketReceived(p);
	}
	for (const auto& task : tasks) {
		task();
	}

	// Both directions send them: the client to time out a silent host, the
	// host to keep every NAT mapping open.
	if (!disconnectNotified) {
		keepAliveTimer += deltaTime;
		if (keepAliveTimer > 1.0f) {
			keepAliveTimer = 0.0f;
			sendPacket(std::make_shared<KeepAlivePacket>());
		}
	}

	if (!isServer()) {
		timeSinceLastKeepAlive += deltaTime;
		if (timeSinceLastKeepAlive > 3.0f && !disconnectNotified) {
			disconnectNotified = true;
			if (ondisconnected) ondisconnected();
			return;
		}

		if (!disconnectNotified) {
			pingTimer += deltaTime;
			if (pingTimer >= 1.0f) {
				pingTimer = 0.0f;
				uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
				auto ping = std::make_shared<PingPacket>();
				ping->timestamp = now;
				sendPacket(ping);
			}
		}
	} else {
		currentPing.store(0, std::memory_order_relaxed);
	}

	// Ease remote nodes toward the last position we heard about
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

	// Send each local node's position, throttled to a fixed network tick rate
	networkTimer += deltaTime;
	if (networkTimer >= 0.05f) {
		networkTimer = 0.f;

		for (auto& kv : nodes) {
			if (!kv.second.local) continue;

			broadcastState(kv.first, kv.second.node->getPosX(), kv.second.node->getPosY(),
			               kv.second.node->getPosZ(), kv.second.localYaw, localTeam, kv.second.localAnimState);
		}
	}
}

void GameBackend::setLocalYaw(uint32_t netId, float yaw) {
	auto it = nodes.find(netId);
	if (it != nodes.end()) it->second.localYaw = yaw;
}

float GameBackend::getRemoteYaw(uint32_t netId) {
	auto it = nodes.find(netId);
	return it != nodes.end() ? it->second.targetYaw : 0.0f;
}

void GameBackend::setLocalAnimState(uint32_t netId, uint8_t animState) {
	auto it = nodes.find(netId);
	if (it != nodes.end()) it->second.localAnimState = animState;
}

uint8_t GameBackend::getRemoteAnimState(uint32_t netId) {
	auto it = nodes.find(netId);
	return it != nodes.end() ? it->second.targetAnimState : 0;
}

void GameBackend::sendFireEvent(uint32_t shooterId, uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz) {
	broadcastFireEvent(shooterId, gunType, ox, oy, oz, dx, dy, dz);
}

void GameBackend::sendHitEvent(uint32_t attackerId, uint32_t victimId, float damage) {
	broadcastHitEvent(attackerId, victimId, damage);
}

void GameBackend::sendKillEvent(uint32_t killerId, uint32_t victimId) {
	broadcastKillEvent(killerId, victimId);
}

void GameBackend::setOnPlayerFired(std::function<void(uint32_t, uint8_t, float, float, float, float, float, float)> cb) {
	onplayerfired = std::move(cb);
}

void GameBackend::setOnPlayerHit(std::function<void(uint32_t, uint32_t, float)> cb) {
	onplayerhit = std::move(cb);
}

void GameBackend::setOnPlayerKilled(std::function<void(uint32_t, uint32_t)> cb) {
	onplayerkilled = std::move(cb);
}

void GameBackend::onPongReceived(uint64_t timestamp) {
	uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	int rtt = (now >= timestamp) ? static_cast<int>(now - timestamp) : 0;
	currentPing.store(rtt, std::memory_order_relaxed);
}
