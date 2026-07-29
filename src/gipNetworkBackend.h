#pragma once

#include "gipMultiplayer.h"
#include <mutex>
#include <vector>
#include <memory>
#include <functional>

// A generic base class for a network backend.
// It safely manages znet initialization and provides a thread-safe queue
// so network packets can be processed on the main game thread.
class gipNetworkBackend {
public:
    gipNetworkBackend();
    virtual ~gipNetworkBackend();

    // Call once per frame on your main game thread.
    // Drains the packet queue and fires onPacketReceived for each packet.
    void update(float deltaTime);

    // Enqueue a packet from a background network thread.
    void enqueuePacket(std::shared_ptr<znet::Packet> packet);

    // Callbacks for connection state
    void setOnConnected(std::function<void()> cb);
    void setOnDisconnected(std::function<void()> cb);

protected:
    // Implemented by your game to handle incoming packets safely on the main thread.
    virtual void onPacketReceived(std::shared_ptr<znet::Packet> packet) = 0;
    
    void notifyConnected();
    void notifyDisconnected();

private:
    std::mutex queueMutex;
    std::vector<std::shared_ptr<znet::Packet>> packetQueue;

    std::function<void()> onconnected;
    std::function<void()> ondisconnected;
};
