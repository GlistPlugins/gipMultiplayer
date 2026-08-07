#pragma once
#include "znet/packet.h"
#include "znet/packet_serializer.h"
#include "znet/buffer.h"
#include <string>
#include <vector>

// Define unique packet IDs for the generic master server
enum MasterPacketId {
    PACKET_GIP_MASTER_REGISTER = 300,
    PACKET_GIP_MASTER_HEARTBEAT = 301,
    PACKET_GIP_MASTER_GET_LIST = 302,
    PACKET_GIP_MASTER_SEND_LIST = 303
};

struct gServerInfo {
    std::string ip;
    std::string name;
    uint32_t currentPlayers;
    uint32_t maxPlayers;
    float lastHeartbeat = 0.0f;
};

class gMasterRegisterPacket : public znet::Packet {
public:
    gMasterRegisterPacket() : Packet(PACKET_GIP_MASTER_REGISTER) {}
    std::string ip;
    std::string name;
    uint32_t currentPlayers;
    uint32_t maxPlayers;
};

class gMasterRegisterSerializer : public znet::PacketSerializer<gMasterRegisterPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterRegisterPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->ip);
        b->WriteString(p->name);
        b->WriteInt(p->currentPlayers);
        b->WriteInt(p->maxPlayers);
        return b;
    }
    std::shared_ptr<gMasterRegisterPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterRegisterPacket>();
        p->ip = b->ReadString();
        p->name = b->ReadString();
        p->currentPlayers = b->ReadInt<uint32_t>();
        p->maxPlayers = b->ReadInt<uint32_t>();
        return p;
    }
};

class gMasterHeartbeatPacket : public znet::Packet {
public:
    gMasterHeartbeatPacket() : Packet(PACKET_GIP_MASTER_HEARTBEAT) {}
    std::string ip;
};

class gMasterHeartbeatSerializer : public znet::PacketSerializer<gMasterHeartbeatPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterHeartbeatPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->ip);
        return b;
    }
    std::shared_ptr<gMasterHeartbeatPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterHeartbeatPacket>();
        p->ip = b->ReadString();
        return p;
    }
};

class gMasterGetListPacket : public znet::Packet {
public:
    gMasterGetListPacket() : Packet(PACKET_GIP_MASTER_GET_LIST) {}
};

class gMasterGetListSerializer : public znet::PacketSerializer<gMasterGetListPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterGetListPacket> p, std::shared_ptr<znet::Buffer> b) override {
        return b;
    }
    std::shared_ptr<gMasterGetListPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        return std::make_shared<gMasterGetListPacket>();
    }
};

class gMasterSendListPacket : public znet::Packet {
public:
    gMasterSendListPacket() : Packet(PACKET_GIP_MASTER_SEND_LIST) {}
    std::vector<gServerInfo> servers;
};

class gMasterSendListSerializer : public znet::PacketSerializer<gMasterSendListPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterSendListPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteInt<int>(p->servers.size());
        for (const auto& s : p->servers) {
            b->WriteString(s.ip);
            b->WriteString(s.name);
            b->WriteInt(s.currentPlayers);
            b->WriteInt(s.maxPlayers);
        }
        return b;
    }
    std::shared_ptr<gMasterSendListPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterSendListPacket>();
        int count = b->ReadInt<int>();
        for (int i = 0; i < count; i++) {
            gServerInfo s;
            s.ip = b->ReadString();
            s.name = b->ReadString();
            s.currentPlayers = b->ReadInt<uint32_t>();
            s.maxPlayers = b->ReadInt<uint32_t>();
            p->servers.push_back(s);
        }
        return p;
    }
};
