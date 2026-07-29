#include "gipNetworkBackend.h"

static bool znet_initialized = false;
static int znet_instances = 0;

gipNetworkBackend::gipNetworkBackend() {
    if (!znet_initialized) {
        if (znet::Init() == znet::Result::Success) {
            znet_initialized = true;
        }
    }
    znet_instances++;
}

gipNetworkBackend::~gipNetworkBackend() {
    znet_instances--;
    if (znet_instances == 0 && znet_initialized) {
        znet::Cleanup();
        znet_initialized = false;
    }
}

void gipNetworkBackend::enqueuePacket(std::shared_ptr<znet::Packet> packet) {
    std::lock_guard<std::mutex> lock(queueMutex);
    packetQueue.push_back(packet);
}

void gipNetworkBackend::update(float deltaTime) {
    std::vector<std::shared_ptr<znet::Packet>> batch;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        batch.swap(packetQueue);
    }

    for (const auto& packet : batch) {
        onPacketReceived(packet);
    }
}

void gipNetworkBackend::setOnConnected(std::function<void()> cb) {
    onconnected = std::move(cb);
}

void gipNetworkBackend::setOnDisconnected(std::function<void()> cb) {
    ondisconnected = std::move(cb);
}

void gipNetworkBackend::notifyConnected() {
    if (onconnected) onconnected();
}

void gipNetworkBackend::notifyDisconnected() {
    if (ondisconnected) ondisconnected();
}
