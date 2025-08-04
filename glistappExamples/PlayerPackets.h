#pragma once

#include "znet/znet.h"

using namespace znet;

enum : PacketId { PACKET_PLAYER_STATE = 1001 };

class PlayerStatePacket : public Packet {
public:
    PlayerStatePacket() : Packet(PACKET_PLAYER_STATE) {}
    uint32_t pid = 0;
    float x = 0.f;
    float y = 0.f;
    uint8_t isEmpty = 0;
};

class PlayerStateSerializer : public PacketSerializer<PlayerStatePacket> {
public:
    std::shared_ptr<Buffer> SerializeTyped(std::shared_ptr<PlayerStatePacket> p, std::shared_ptr<Buffer> b) override {
        b->WriteInt<uint32_t>(p->pid);
        b->Write(reinterpret_cast<const char*>(&p->x), sizeof(float));
        b->Write(reinterpret_cast<const char*>(&p->y), sizeof(float));
        b->WriteInt<uint8_t>(p->isEmpty);

        return b;
    }

    std::shared_ptr<PlayerStatePacket> DeserializeTyped(std::shared_ptr<Buffer> b) override {
        auto p = std::make_shared<PlayerStatePacket>();
        p->pid = b->ReadInt<uint32_t>();
        b->Read(reinterpret_cast<char*>(&p->x), sizeof(float));
        b->Read(reinterpret_cast<char*>(&p->y), sizeof(float));
        p->isEmpty = b->ReadInt<uint8_t>();

        return p;
    }
};