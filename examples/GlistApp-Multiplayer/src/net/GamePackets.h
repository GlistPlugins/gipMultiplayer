/*
 * GamePackets.h
 *
 * Defines the packet types and their serializers for network communication.
 * Each packet needs a unique ID, a data class, and a serializer that converts
 * it to/from a byte buffer for transmission over the network.
 */

#ifndef GAMEPACKETS_H
#define GAMEPACKETS_H

#include "gipMultiplayer.h"

// Packet IDs - must be unique per packet type
enum : znet::PacketId {
    PACKET_NODE_STATE,
    PACKET_NODE_LEAVE
};

// Sent every frame to sync a node's position across the network
class NodeStatePacket : public znet::Packet {
public:
    NodeStatePacket() : Packet(PACKET_NODE_STATE) {}
    uint32_t netid = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

class NodeStateSerializer : public znet::PacketSerializer<NodeStatePacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<NodeStatePacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteInt<uint32_t>(p->netid);
        b->WriteFloat(p->x);
        b->WriteFloat(p->y);
        b->WriteFloat(p->z);
        return b;
    }

    std::shared_ptr<NodeStatePacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<NodeStatePacket>();
        p->netid = b->ReadInt<uint32_t>();
        p->x = b->ReadFloat();
        p->y = b->ReadFloat();
        p->z = b->ReadFloat();
        return p;
    }
};

// Sent when a node disconnects so others can remove it
class NodeLeavePacket : public znet::Packet {
public:
    NodeLeavePacket() : Packet(PACKET_NODE_LEAVE) {}
    uint32_t netid = 0;
};

class NodeLeaveSerializer : public znet::PacketSerializer<NodeLeavePacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<NodeLeavePacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteInt<uint32_t>(p->netid);
        return b;
    }

    std::shared_ptr<NodeLeavePacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<NodeLeavePacket>();
        p->netid = b->ReadInt<uint32_t>();
        return p;
    }
};

#endif //GAMEPACKETS_H
