//
// Created by Yusuf Ustaoglu on 5.08.2025.
//

#ifndef PACKETS_H
#define PACKETS_H

#include "znet/znet.h"
using namespace znet;

enum : PacketId {
    PACKET_PLAYER_STATE,
    PACKET_PLAYER_DISCONNECT
};

class PlayerStatePacket : public Packet {
public:
    PlayerStatePacket() : Packet(PACKET_PLAYER_STATE) {}
    uint32_t pid = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

class PlayerStateSerializer : public PacketSerializer<PlayerStatePacket> {
public:
    std::shared_ptr<Buffer> SerializeTyped(std::shared_ptr<PlayerStatePacket> p, std::shared_ptr<Buffer> b) override {
        b->WriteInt<uint32_t>(p->pid);
        b->Write(reinterpret_cast<const char*>(&p->x), sizeof(float));
        b->Write(reinterpret_cast<const char*>(&p->y), sizeof(float));
        b->Write(reinterpret_cast<const char*>(&p->z), sizeof(float));
        return b;
    }

    std::shared_ptr<PlayerStatePacket> DeserializeTyped(std::shared_ptr<Buffer> b) override {
        auto p = std::make_shared<PlayerStatePacket>();
        p->pid = b->ReadInt<uint32_t>();
        b->Read(reinterpret_cast<char*>(&p->x), sizeof(float));
        b->Read(reinterpret_cast<char*>(&p->y), sizeof(float));
        b->Read(reinterpret_cast<char*>(&p->z), sizeof(float));
        return p;
    }
};

class PlayerDisconnectPacket : public Packet {
public:
    PlayerDisconnectPacket() : Packet(PACKET_PLAYER_DISCONNECT) {}
    uint32_t pid = 0;
};

class PlayerDisconnectSerializer : public PacketSerializer<PlayerDisconnectPacket> {
public:
    std::shared_ptr<Buffer> SerializeTyped(std::shared_ptr<PlayerDisconnectPacket> p, std::shared_ptr<Buffer> b) override {
        b->WriteInt<uint32_t>(p->pid);
        return b;
    }
    std::shared_ptr<PlayerDisconnectPacket> DeserializeTyped(std::shared_ptr<Buffer> b) override {
        auto p = std::make_shared<PlayerDisconnectPacket>();
        p->pid = b->ReadInt<uint32_t>();
        return p;
    }
};

#endif //PACKETS_H
