#pragma once
#include "znet/packet.h"
#include "znet/packet_serializer.h"
#include "znet/buffer.h"
#include "znet/peer_session.h"
#include <string>
#include <vector>

// Define unique packet IDs for the generic master server
enum MasterPacketId {
    PACKET_GIP_MASTER_REGISTER = 300,
    PACKET_GIP_MASTER_HEARTBEAT = 301,
    PACKET_GIP_MASTER_GET_LIST = 302,
    PACKET_GIP_MASTER_SEND_LIST = 303,
    PACKET_GIP_MASTER_REGISTER_RES = 304,
    PACKET_GIP_MASTER_PUNCH_REQ = 305,
    PACKET_GIP_MASTER_PUNCH_EXEC = 306,
    PACKET_GIP_MASTER_QUERY_ROOM = 307,
    PACKET_GIP_MASTER_QUERY_ROOM_RES = 308
};

struct gServerInfo {
    std::string ip;
    std::string name;
    uint32_t currentPlayers;
    uint32_t maxPlayers;
    int matchState;
    float lastHeartbeat = 0.0f;
    bool isPrivate = false;
    bool hasPassword = false;
    bool isDedicated = false;
    std::string roomCode = "";
    znet::PeerSession* hostSession = nullptr;
};

class gMasterRegisterPacket : public znet::Packet {
public:
    gMasterRegisterPacket() : Packet(PACKET_GIP_MASTER_REGISTER) {}
    std::string ip;
    std::string name;
    uint32_t currentPlayers;
    uint32_t maxPlayers;
    int matchState;
    bool isPrivate = false;
    bool hasPassword = false;
    bool isDedicated = false;
};

class gMasterRegisterSerializer : public znet::PacketSerializer<gMasterRegisterPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterRegisterPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->ip);
        b->WriteString(p->name);
        b->WriteInt(p->currentPlayers);
        b->WriteInt(p->maxPlayers);
        b->WriteInt(p->matchState);
        b->WriteInt(p->isPrivate ? 1 : 0);
        b->WriteInt(p->hasPassword ? 1 : 0);
        b->WriteInt(p->isDedicated ? 1 : 0);
        return b;
    }
    std::shared_ptr<gMasterRegisterPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterRegisterPacket>();
        p->ip = b->ReadString();
        p->name = b->ReadString();
        p->currentPlayers = b->ReadInt<uint32_t>();
        p->maxPlayers = b->ReadInt<uint32_t>();
        p->matchState = b->ReadInt<int>();
        p->isPrivate = b->ReadInt<int>() != 0;
        p->hasPassword = b->ReadInt<int>() != 0;
        p->isDedicated = b->ReadInt<int>() != 0;
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
    int matchStateFilter = -1;
};

class gMasterGetListSerializer : public znet::PacketSerializer<gMasterGetListPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterGetListPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteInt<int>(p->matchStateFilter);
        return b;
    }
    std::shared_ptr<gMasterGetListPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterGetListPacket>();
        p->matchStateFilter = b->ReadInt<int>();
        return p;
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
            b->WriteInt(s.matchState);
            b->WriteInt(s.isPrivate ? 1 : 0);
            b->WriteInt(s.hasPassword ? 1 : 0);
            b->WriteInt(s.isDedicated ? 1 : 0);
            b->WriteString(s.roomCode);
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
            s.matchState = b->ReadInt<int>();
            s.isPrivate = b->ReadInt<int>() != 0;
            s.hasPassword = b->ReadInt<int>() != 0;
            s.isDedicated = b->ReadInt<int>() != 0;
            s.roomCode = b->ReadString();
            p->servers.push_back(s);
        }
        return p;
    }
};

class gMasterRegisterResponsePacket : public znet::Packet {
public:
    gMasterRegisterResponsePacket() : Packet(PACKET_GIP_MASTER_REGISTER_RES) {}
    std::string roomCode;
};

class gMasterRegisterResponseSerializer : public znet::PacketSerializer<gMasterRegisterResponsePacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterRegisterResponsePacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->roomCode);
        return b;
    }
    std::shared_ptr<gMasterRegisterResponsePacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterRegisterResponsePacket>();
        p->roomCode = b->ReadString();
        return p;
    }
};

class gMasterPunchRequestPacket : public znet::Packet {
public:
    gMasterPunchRequestPacket() : Packet(PACKET_GIP_MASTER_PUNCH_REQ) {}
    std::string targetIdentifier;
    std::string clientIp;
    uint16_t clientGamePort;
};

class gMasterPunchRequestSerializer : public znet::PacketSerializer<gMasterPunchRequestPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterPunchRequestPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->targetIdentifier);
        b->WriteString(p->clientIp);
        b->WriteInt<uint16_t>(p->clientGamePort);
        return b;
    }
    std::shared_ptr<gMasterPunchRequestPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterPunchRequestPacket>();
        p->targetIdentifier = b->ReadString();
        p->clientIp = b->ReadString();
        p->clientGamePort = b->ReadInt<uint16_t>();
        return p;
    }
};

class gMasterPunchExecutePacket : public znet::Packet {
public:
    gMasterPunchExecutePacket() : Packet(PACKET_GIP_MASTER_PUNCH_EXEC) {}
    std::string remoteIp;
    uint16_t remotePort;
    bool isHost;
};

class gMasterPunchExecuteSerializer : public znet::PacketSerializer<gMasterPunchExecutePacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterPunchExecutePacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->remoteIp);
        b->WriteInt<uint16_t>(p->remotePort);
        b->WriteInt<int>(p->isHost ? 1 : 0);
        return b;
    }
    std::shared_ptr<gMasterPunchExecutePacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterPunchExecutePacket>();
        p->remoteIp = b->ReadString();
        p->remotePort = b->ReadInt<uint16_t>();
        p->isHost = b->ReadInt<int>() == 1;
        return p;
    }
};

class gMasterQueryRoomPacket : public znet::Packet {
public:
    gMasterQueryRoomPacket() : Packet(PACKET_GIP_MASTER_QUERY_ROOM) {}
    std::string roomCode;
};

class gMasterQueryRoomSerializer : public znet::PacketSerializer<gMasterQueryRoomPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterQueryRoomPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->roomCode);
        return b;
    }
    std::shared_ptr<gMasterQueryRoomPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterQueryRoomPacket>();
        p->roomCode = b->ReadString();
        return p;
    }
};

class gMasterQueryRoomResPacket : public znet::Packet {
public:
    gMasterQueryRoomResPacket() : Packet(PACKET_GIP_MASTER_QUERY_ROOM_RES) {}
    std::string roomCode;
    bool found = false;
    std::string name;
    uint32_t currentPlayers = 0;
    uint32_t maxPlayers = 0;
    bool hasPassword = false;
    bool isDedicated = false;
    std::string ip = "";
};

class gMasterQueryRoomResSerializer : public znet::PacketSerializer<gMasterQueryRoomResPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterQueryRoomResPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->roomCode);
        b->WriteBool(p->found);
        if (p->found) {
            b->WriteString(p->name);
            b->WriteInt<uint32_t>(p->currentPlayers);
            b->WriteInt<uint32_t>(p->maxPlayers);
            b->WriteBool(p->hasPassword);
            b->WriteBool(p->isDedicated);
            b->WriteString(p->ip);
        }
        return b;
    }
    std::shared_ptr<gMasterQueryRoomResPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterQueryRoomResPacket>();
        p->roomCode = b->ReadString();
        p->found = b->ReadBool();
        if (p->found) {
            p->name = b->ReadString();
            p->currentPlayers = b->ReadInt<uint32_t>();
            p->maxPlayers = b->ReadInt<uint32_t>();
            p->hasPassword = b->ReadBool();
            p->isDedicated = b->ReadBool();
            p->ip = b->ReadString();
        }
        return p;
    }
};
