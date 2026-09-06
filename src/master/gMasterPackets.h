#pragma once
#include "znet/packet.h"
#include "znet/packet_serializer.h"
#include "znet/buffer.h"
#include "znet/p2p/punch.h"
#include "znet/p2p/rendezvous.h"
#include "znet/peer_session.h"
#include <memory>
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
    PACKET_GIP_MASTER_QUERY_ROOM_RES = 308,
    
    // User Authentication
    PACKET_GIP_MASTER_USER_LOGIN = 309,
    PACKET_GIP_MASTER_USER_LOGIN_RES = 310,
    PACKET_GIP_MASTER_USER_REGISTER = 311,
    PACKET_GIP_MASTER_USER_REGISTER_RES = 312,
    PACKET_GIP_MASTER_USER_TOKEN_LOGIN = 313,
    PACKET_GIP_MASTER_USER_LOGOUT = 314
};

struct gServerInfo {
    std::string ip;
    std::vector<std::string> peerCandidates;
    // What the host gathered from its punch socket (znet::p2p::Agent::Gather);
    // empty for a host that does not punch.
    std::vector<znet::p2p::Candidate> candidates;
    std::string name;
    uint32_t currentPlayers = 0;
    uint32_t maxPlayers = 0;
    int matchState = 0;
    float lastHeartbeat = 0.0f;
    bool isPrivate = false;
    bool hasPassword = false;
    bool isDedicated = false;
    bool useP2P = false;
    std::string roomCode = "";
    // Weak, so a host that drops leaves an expired handle rather than a
    // dangling pointer. The entry itself lingers until the heartbeat prunes it.
    std::weak_ptr<znet::PeerSession> hostSession;
};

class gMasterRegisterPacket : public znet::Packet {
public:
    gMasterRegisterPacket() : Packet(PACKET_GIP_MASTER_REGISTER) {}
    // Advertised address; every candidate below shares its port.
    std::string ip;
    std::string name;
    uint32_t currentPlayers = 0;
    uint32_t maxPlayers = 0;
    int matchState = 0;
    bool isPrivate = false;
    bool hasPassword = false;
    bool isDedicated = false;
    bool useP2P = false;
    // What the punch socket gathered; reflexive first. Empty until it has.
    std::vector<znet::p2p::Candidate> candidates;
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
        b->WriteInt(p->useP2P ? 1 : 0);
        znet::p2p::detail::WriteCandidates(b, p->candidates);
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
        p->useP2P = b->ReadInt<int>() != 0;
        if (!znet::p2p::detail::ReadCandidates(b, p->candidates)) return nullptr;
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
        b->WriteVarInt(p->servers.size());
        for (const auto& s : p->servers) {
            b->WriteString(s.ip);
            b->WriteString(s.name);
            b->WriteInt(s.currentPlayers);
            b->WriteInt(s.maxPlayers);
            b->WriteInt(s.matchState);
            b->WriteInt(s.isPrivate ? 1 : 0);
            b->WriteInt(s.hasPassword ? 1 : 0);
            b->WriteInt(s.isDedicated ? 1 : 0);
            b->WriteInt(s.useP2P ? 1 : 0);
            b->WriteString(s.roomCode);
        }
        return b;
    }
    std::shared_ptr<gMasterSendListPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterSendListPacket>();
        size_t count = b->ReadVarInt<size_t>();
        // One string minimum per entry, so a count past the buffer is corrupt.
        if (count > b->readable_bytes()) return p;
        for (size_t i = 0; i < count; i++) {
            gServerInfo s;
            s.ip = b->ReadString();
            s.name = b->ReadString();
            s.currentPlayers = b->ReadInt<uint32_t>();
            s.maxPlayers = b->ReadInt<uint32_t>();
            s.matchState = b->ReadInt<int>();
            s.isPrivate = b->ReadInt<int>() != 0;
            s.hasPassword = b->ReadInt<int>() != 0;
            s.isDedicated = b->ReadInt<int>() != 0;
            s.useP2P = b->ReadInt<int>() != 0;
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
    // What the client's punch socket gathered; reflexive first.
    std::vector<znet::p2p::Candidate> candidates;
    uint16_t clientGamePort = 0;
};

class gMasterPunchRequestSerializer : public znet::PacketSerializer<gMasterPunchRequestPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterPunchRequestPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->targetIdentifier);
        znet::p2p::detail::WriteCandidates(b, p->candidates);
        b->WriteInt<uint16_t>(p->clientGamePort);
        return b;
    }
    std::shared_ptr<gMasterPunchRequestPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterPunchRequestPacket>();
        p->targetIdentifier = b->ReadString();
        if (!znet::p2p::detail::ReadCandidates(b, p->candidates)) return nullptr;
        p->clientGamePort = b->ReadInt<uint16_t>();
        return p;
    }
};

class gMasterPunchExecutePacket : public znet::Packet {
public:
    gMasterPunchExecutePacket() : Packet(PACKET_GIP_MASTER_PUNCH_EXEC) {}
    // The other side's candidates, reflexive first, then host, then the
    // relayed one when the master runs a relay. An unspecified host
    // (0.0.0.0) means the master's own.
    std::vector<znet::p2p::Candidate> candidates;
    bool isHost = false;
};

class gMasterPunchExecuteSerializer : public znet::PacketSerializer<gMasterPunchExecutePacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterPunchExecutePacket> p, std::shared_ptr<znet::Buffer> b) override {
        znet::p2p::detail::WriteCandidates(b, p->candidates);
        b->WriteInt<int>(p->isHost ? 1 : 0);
        return b;
    }
    std::shared_ptr<gMasterPunchExecutePacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterPunchExecutePacket>();
        if (!znet::p2p::detail::ReadCandidates(b, p->candidates)) return nullptr;
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
    bool useP2P = false;
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
            b->WriteBool(p->useP2P);
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
            p->useP2P = b->ReadBool();
            p->ip = b->ReadString();
        }
        return p;
    }
};

// User Auth Packets
class gMasterUserLoginPacket : public znet::Packet {
public:
    gMasterUserLoginPacket() : Packet(PACKET_GIP_MASTER_USER_LOGIN) {}
    std::string email;
    std::string password;
};

class gMasterUserLoginSerializer : public znet::PacketSerializer<gMasterUserLoginPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterUserLoginPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->email);
        b->WriteString(p->password);
        return b;
    }
    std::shared_ptr<gMasterUserLoginPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterUserLoginPacket>();
        p->email = b->ReadString();
        p->password = b->ReadString();
        return p;
    }
};

class gMasterUserLoginResPacket : public znet::Packet {
public:
    gMasterUserLoginResPacket() : Packet(PACKET_GIP_MASTER_USER_LOGIN_RES) {}
    bool success = false;
    std::string message;
    std::string username;
    std::string token;
};

class gMasterUserLoginResSerializer : public znet::PacketSerializer<gMasterUserLoginResPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterUserLoginResPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteBool(p->success);
        b->WriteString(p->message);
        b->WriteString(p->username);
        b->WriteString(p->token);
        return b;
    }
    std::shared_ptr<gMasterUserLoginResPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterUserLoginResPacket>();
        p->success = b->ReadBool();
        p->message = b->ReadString();
        p->username = b->ReadString();
        p->token = b->ReadString();
        return p;
    }
};

class gMasterUserRegisterPacket : public znet::Packet {
public:
    gMasterUserRegisterPacket() : Packet(PACKET_GIP_MASTER_USER_REGISTER) {}
    std::string username;
    std::string email;
    std::string password;
};

class gMasterUserRegisterSerializer : public znet::PacketSerializer<gMasterUserRegisterPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterUserRegisterPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->username);
        b->WriteString(p->email);
        b->WriteString(p->password);
        return b;
    }
    std::shared_ptr<gMasterUserRegisterPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterUserRegisterPacket>();
        p->username = b->ReadString();
        p->email = b->ReadString();
        p->password = b->ReadString();
        return p;
    }
};

class gMasterUserRegisterResPacket : public znet::Packet {
public:
    gMasterUserRegisterResPacket() : Packet(PACKET_GIP_MASTER_USER_REGISTER_RES) {}
    bool success = false;
    std::string message;
    std::string token;
};

class gMasterUserRegisterResSerializer : public znet::PacketSerializer<gMasterUserRegisterResPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterUserRegisterResPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteBool(p->success);
        b->WriteString(p->message);
        b->WriteString(p->token);
        return b;
    }
    std::shared_ptr<gMasterUserRegisterResPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterUserRegisterResPacket>();
        p->success = b->ReadBool();
        p->message = b->ReadString();
        p->token = b->ReadString();
        return p;
    }
};

class gMasterUserTokenLoginPacket : public znet::Packet {
public:
    gMasterUserTokenLoginPacket() : Packet(PACKET_GIP_MASTER_USER_TOKEN_LOGIN) {}
    std::string email;
    std::string token;
};

class gMasterUserTokenLoginSerializer : public znet::PacketSerializer<gMasterUserTokenLoginPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterUserTokenLoginPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->email);
        b->WriteString(p->token);
        return b;
    }
    std::shared_ptr<gMasterUserTokenLoginPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterUserTokenLoginPacket>();
        p->email = b->ReadString();
        p->token = b->ReadString();
        return p;
    }
};

class gMasterUserLogoutPacket : public znet::Packet {
public:
    gMasterUserLogoutPacket() : Packet(PACKET_GIP_MASTER_USER_LOGOUT) {}
    std::string email;
    std::string token;
};

class gMasterUserLogoutSerializer : public znet::PacketSerializer<gMasterUserLogoutPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gMasterUserLogoutPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->email);
        b->WriteString(p->token);
        return b;
    }
    std::shared_ptr<gMasterUserLogoutPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<gMasterUserLogoutPacket>();
        p->email = b->ReadString();
        p->token = b->ReadString();
        return p;
    }
};
