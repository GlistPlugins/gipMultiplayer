#pragma once

#include "NetworkManager.h"
#include "gBox.h"
#include <unordered_map>
#include <memory>

class NetworkSynchronizer {
public:
    static NetworkSynchronizer* getInstance();

    uint32_t getLocalNodeId() const { return localmultiplayerboxid; }
    void regenerateLocalNodeId();

    // Call this once when the game loads
    void setup();

    // Call this every frame to sync local player position and process packets
    void update(float deltaTime, float cameraX, float cameraY, float cameraZ, float cameraYaw, uint8_t animState = 0);

    // Clean up when the game ends
    void cleanup();

    // Example feature: switch teams
    void switchTeam();

    const std::unordered_map<uint32_t, std::shared_ptr<gBox>>& getRemotePlayers() const { return remotemultiplayerboxes; }
    
    uint8_t getRemoteTeam(uint32_t id) const {
        auto it = remoteteams.find(id);
        return it != remoteteams.end() ? it->second : 1;
    }

    void sendFireEvent(uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz);
    void sendHitEvent(uint32_t victimId, float damage);
    void sendKillEvent(uint32_t killerId, uint32_t victimId);

    void setOnRemoteFire(std::function<void(uint32_t, uint8_t, float, float, float, float, float, float)> cb);
    void setOnRemoteHit(std::function<void(uint32_t, uint32_t, float)> cb);
    void setOnRemoteKilled(std::function<void(uint32_t, uint32_t)> cb);


private:
    NetworkSynchronizer();
    ~NetworkSynchronizer() = default;

    uint32_t localmultiplayerboxid = 0;
    std::shared_ptr<gBox> localmultiplayerbox = std::make_shared<gBox>();

    std::unordered_map<uint32_t, std::shared_ptr<gBox>> remotemultiplayerboxes;
    float networktimer = 0.0f;
    std::unordered_map<uint32_t, uint8_t> remoteteams;

    std::function<void(uint32_t, uint8_t, float, float, float, float, float, float)> onRemoteFire;
    std::function<void(uint32_t, uint32_t, float)> onRemoteHit;
    std::function<void(uint32_t, uint32_t)> onRemoteKilled;
};
