#include "NetworkSynchronizer.h"
#include <iostream>
#include <random>
#include <chrono>

static uint32_t MakeMultiplayerNodeId() {
    static std::mt19937 ridg((uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    static uint32_t cachedid = ridg();
    return cachedid;
}

NetworkSynchronizer::NetworkSynchronizer() {
    localmultiplayerboxid = MakeMultiplayerNodeId();
}

void NetworkSynchronizer::regenerateLocalNodeId() {
    std::mt19937 ridg((uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    localmultiplayerboxid = ridg();
}

NetworkSynchronizer* NetworkSynchronizer::getInstance() {
    static NetworkSynchronizer instance;
    return &instance;
}

void NetworkSynchronizer::setup() {
    auto backend = NetworkManager::getInstance()->getBackend();
    if (!backend) return;

    // Boxes from an earlier match would otherwise be drawn for a lobby that
    // never joined them.
    remotemultiplayerboxes.clear();
    remoteteams.clear();
    networktimer = 0.0f;

    localmultiplayerbox->setScale(0.12f);
    localmultiplayerbox->setPosition(1.5f, 0.25f, 1.5f);

    backend->attachNode(localmultiplayerboxid, localmultiplayerbox, true);
    
    uint8_t myTeam = 1;
    for (auto& rp : backend->roomPlayers) {
        if (rp.id == localmultiplayerboxid) {
            myTeam = rp.team;
            break;
        }
    }
    backend->setLocalTeam(myTeam);

    backend->setOnTeamChanged([this](uint32_t id, uint8_t teamId) {
        remoteteams[id] = teamId;
        std::cout << "Multiplayer: Remote Player " << id << " switched to Team " << (int)teamId << std::endl;
    });

    backend->setOnJoin([this](uint32_t id) {
        auto backend = NetworkManager::getInstance()->getBackend();
        if (!backend) return;
        if (id == localmultiplayerboxid) return;

        auto box = std::make_shared<gBox>();
        box->setScale(0.12f);
        box->setPosition(1.5f, 0.25f, 1.5f);

        backend->attachNode(id, box, false);
        remotemultiplayerboxes[id] = std::move(box);
    });

    backend->setOnLeave([this](uint32_t id) {
        auto backend = NetworkManager::getInstance()->getBackend();
        if (backend) backend->detachNode(id);
        remotemultiplayerboxes.erase(id);
        remoteteams.erase(id);
    });

    backend->setOnPlayerFired([this](uint32_t shooterId, uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz) {
        if (onRemoteFire) onRemoteFire(shooterId, gunType, ox, oy, oz, dx, dy, dz);
      });

    backend->setOnPlayerHit([this](uint32_t attackerId, uint32_t victimId, float damage) {
         if (onRemoteHit) onRemoteHit(attackerId, victimId, damage);
      });

    backend->setOnPlayerKilled([this](uint32_t killerId, uint32_t victimId) {
        if (onRemoteKilled) onRemoteKilled(killerId, victimId);
    });
}

void NetworkSynchronizer::update(float deltaTime, float cameraX, float cameraY, float cameraZ, float cameraYaw, uint8_t animState) {
    auto backend = NetworkManager::getInstance()->getBackend();
    if (!backend) return;

    networktimer += deltaTime;
    if (networktimer >= 0.05f) {
        networktimer = 0.0f;
        localmultiplayerbox->setPosition(cameraX, cameraY - 0.175f, cameraZ);
        backend->setLocalYaw(localmultiplayerboxid, cameraYaw);
        backend->setLocalAnimState(localmultiplayerboxid, animState);
    }
}



void NetworkSynchronizer::switchTeam() {
    auto backend = NetworkManager::getInstance()->getBackend();
    if (backend) {
        uint8_t newTeam = backend->getLocalTeam() == 1 ? 2 : 1;
        backend->setLocalTeam(newTeam);
    }
}

void NetworkSynchronizer::cleanup() {
    auto backend = NetworkManager::getInstance()->getBackend();
    if (!backend) return;

    backend->detachNode(localmultiplayerboxid);
    for (auto& kv : remotemultiplayerboxes) {
        backend->detachNode(kv.first);
    }
    remotemultiplayerboxes.clear();
    remoteteams.clear();
}

void NetworkSynchronizer::sendFireEvent(uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz) {
    auto backend = NetworkManager::getInstance()->getBackend();
    if (backend) {
        backend->sendFireEvent(localmultiplayerboxid, gunType, ox, oy, oz, dx, dy, dz);
    }
}

void NetworkSynchronizer::sendHitEvent(uint32_t victimId, float damage) {
    auto backend = NetworkManager::getInstance()->getBackend();
    if (backend) {
        backend->sendHitEvent(localmultiplayerboxid, victimId, damage);
    }
}

void NetworkSynchronizer::sendKillEvent(uint32_t killerId, uint32_t victimId) {
    auto backend = NetworkManager::getInstance()->getBackend();
    if (backend) {
        backend->sendKillEvent(killerId, victimId);
    }
}

//Set Callbacks
void NetworkSynchronizer::setOnRemoteFire(std::function<void(uint32_t, uint8_t, float, float, float, float, float, float)> cb) {
    onRemoteFire = std::move(cb);
}

void NetworkSynchronizer::setOnRemoteHit(std::function<void(uint32_t, uint32_t, float)> cb) {
    onRemoteHit = std::move(cb);
}

void NetworkSynchronizer::setOnRemoteKilled(std::function<void(uint32_t, uint32_t)> cb) {
    onRemoteKilled = std::move(cb);
}
