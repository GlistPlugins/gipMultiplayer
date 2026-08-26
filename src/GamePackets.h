/*
 * GamePackets.h
 *
 * Defines the packet types and their serializers for network communication.
 * Each packet needs a unique ID, a data class, and a serializer that converts
 * it to/from a byte buffer for transmission over the network.
 */

#ifndef GAMEPACKETS_H
#define GAMEPACKETS_H

#include <memory>
#include <string>
#include <vector>
#include "znet/packet.h"
#include "znet/packet_serializer.h"
#include "znet/buffer.h"
#include "znet/peer_session.h"

// Packet IDs - must be unique per packet type
enum : znet::PacketId {
    PACKET_NODE_STATE,
    PACKET_NODE_LEAVE,
	PACKET_NODE_FIRE,
	PACKET_NODE_HIT,
    PACKET_NODE_KILLED,
    PACKET_SERVER_QUERY_REQ,
    PACKET_SERVER_QUERY_RES,
    PACKET_LOBBY_JOIN,
    PACKET_LOBBY_STATE,
    PACKET_TOGGLE_READY,
    PACKET_SWITCH_TEAM,
    PACKET_START_MATCH,
    PACKET_LOBBY_KICK,
    PACKET_KEEPALIVE,
    PACKET_PING,
    PACKET_PONG
};

class KeepAlivePacket : public znet::Packet {
public:
    KeepAlivePacket() : Packet(PACKET_KEEPALIVE) {}
};

class KeepAliveSerializer : public znet::PacketSerializer<KeepAlivePacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<KeepAlivePacket> p, std::shared_ptr<znet::Buffer> b) override {
        return b;
    }
    std::shared_ptr<KeepAlivePacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        return std::make_shared<KeepAlivePacket>();
    }
};

class PingPacket : public znet::Packet {
public:
    PingPacket() : Packet(PACKET_PING) {}
    uint64_t timestamp = 0;
};

class PingSerializer : public znet::PacketSerializer<PingPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<PingPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteInt<uint64_t>(p->timestamp);
        return b;
    }
    std::shared_ptr<PingPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<PingPacket>();
        p->timestamp = b->ReadInt<uint64_t>();
        return p;
    }
};

class PongPacket : public znet::Packet {
public:
    PongPacket() : Packet(PACKET_PONG) {}
    uint64_t timestamp = 0;
};

class PongSerializer : public znet::PacketSerializer<PongPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<PongPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteInt<uint64_t>(p->timestamp);
        return b;
    }
    std::shared_ptr<PongPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<PongPacket>();
        p->timestamp = b->ReadInt<uint64_t>();
        return p;
    }
};

// Sent every frame to sync a node's position across the network
class NodeStatePacket : public znet::Packet {
public:
    NodeStatePacket() : Packet(PACKET_NODE_STATE) {}
    uint32_t netid = 0;
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    float yaw = 0.0f;
    uint8_t team = 0;
    uint8_t animState = 0;
};

class NodeStateSerializer : public znet::PacketSerializer<NodeStatePacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<NodeStatePacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteInt<uint32_t>(p->netid);
        b->WriteFloat(p->x);
        b->WriteFloat(p->y);
        b->WriteFloat(p->z);
        b->WriteFloat(p->yaw);
        b->WriteInt<uint8_t>(p->team);
        b->WriteInt<uint8_t>(p->animState);
        return b;
    }

    std::shared_ptr<NodeStatePacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<NodeStatePacket>();
        p->netid = b->ReadInt<uint32_t>();
        p->x = b->ReadFloat();
        p->y = b->ReadFloat();
        p->z = b->ReadFloat();
        p->yaw = b->ReadFloat();
        p->team = b->ReadInt<uint8_t>();
        p->animState = b->ReadInt<uint8_t>();
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

class PlayerFirePacket : public znet::Packet {
public:
	PlayerFirePacket() : Packet(PACKET_NODE_FIRE) {}
	uint32_t shooterId = 0;
	uint8_t gunType = 0;
	float originX = 0.0f, originY = 0.0f, originZ = 0.0f;
	float dirX = 0.0f, dirY = 0.0f, dirZ = 0.0f;
};

class PlayerFireSerializer : public znet::PacketSerializer<PlayerFirePacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<PlayerFirePacket> p, std::shared_ptr<znet::Buffer> b) override {
		b->WriteInt<uint32_t>(p->shooterId);
		b->WriteInt<uint8_t>(p->gunType);
		b->WriteFloat(p->originX); b->WriteFloat(p->originY); b->WriteFloat(p->originZ);
		b->WriteFloat(p->dirX); b->WriteFloat(p->dirY); b->WriteFloat(p->dirZ);
		return b;
	}
	std::shared_ptr<PlayerFirePacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
		auto p = std::make_shared<PlayerFirePacket>();
		p->shooterId = b->ReadInt<uint32_t>();
		p->gunType = b->ReadInt<uint8_t>();
		p->originX = b->ReadFloat(); p->originY = b->ReadFloat(); p->originZ = b->ReadFloat();
		p->dirX = b->ReadFloat(); p->dirY = b->ReadFloat(); p->dirZ = b->ReadFloat();
		return p;
	}
};

class PlayerHitPacket : public znet::Packet {
public:
	PlayerHitPacket() : Packet(PACKET_NODE_HIT) {}
	uint32_t attackerId = 0;
	uint32_t victimId = 0;
	float damage = 0.0f;
};

class PlayerHitSerializer : public znet::PacketSerializer<PlayerHitPacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<PlayerHitPacket> p, std::shared_ptr<znet::Buffer> b) override {
		b->WriteInt<uint32_t>(p->attackerId);
		b->WriteInt<uint32_t>(p->victimId);
		b->WriteFloat(p->damage);
		return b;
	}
    std::shared_ptr<PlayerHitPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<PlayerHitPacket>();
        p->attackerId = b->ReadInt<uint32_t>();
        p->victimId = b->ReadInt<uint32_t>();
        p->damage = b->ReadFloat();
        return p;
    }
};

class PlayerKilledPacket : public znet::Packet {
public:
    PlayerKilledPacket() : Packet(PACKET_NODE_KILLED) {}
    uint32_t killerId = 0;
    uint32_t victimId = 0;
};

class PlayerKilledSerializer : public znet::PacketSerializer<PlayerKilledPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<PlayerKilledPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteInt<uint32_t>(p->killerId);
        b->WriteInt<uint32_t>(p->victimId);
        return b;
    }

    std::shared_ptr<PlayerKilledPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<PlayerKilledPacket>();
        p->killerId = b->ReadInt<uint32_t>();
        p->victimId = b->ReadInt<uint32_t>();
        return p;
    }
};

class ServerQueryReqPacket : public znet::Packet {
public:
	ServerQueryReqPacket() : Packet(PACKET_SERVER_QUERY_REQ) {}
};

class ServerQueryReqSerializer : public znet::PacketSerializer<ServerQueryReqPacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<ServerQueryReqPacket> p, std::shared_ptr<znet::Buffer> b) override {
		b->WriteInt<uint8_t>(1); // Dummy byte to prevent 0-byte Zstandard crash
		return b;
	}
	std::shared_ptr<ServerQueryReqPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
		b->ReadInt<uint8_t>();
		return std::make_shared<ServerQueryReqPacket>();
	}
};

class ServerQueryResPacket : public znet::Packet {
public:
	ServerQueryResPacket() : Packet(PACKET_SERVER_QUERY_RES) {}
	std::string lobbyName;
	std::string format;
	std::string sizeStr;
    bool isDedicated = false;
};

class ServerQueryResSerializer : public znet::PacketSerializer<ServerQueryResPacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<ServerQueryResPacket> p, std::shared_ptr<znet::Buffer> b) override {
		b->WriteString(p->lobbyName);
		b->WriteString(p->format);
		b->WriteString(p->sizeStr);
        b->WriteInt(p->isDedicated ? 1 : 0);
		return b;
	}
	std::shared_ptr<ServerQueryResPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
		auto p = std::make_shared<ServerQueryResPacket>();
		p->lobbyName = b->ReadString();
		p->format = b->ReadString();
		p->sizeStr = b->ReadString();
        p->isDedicated = b->ReadInt<int>() != 0;
		return p;
	}
};

class LobbyJoinPacket : public znet::Packet {
public:
	LobbyJoinPacket() : Packet(PACKET_LOBBY_JOIN) {}
	uint32_t senderId = 0;
	std::string playerName;
	std::string password;
};

class LobbyJoinSerializer : public znet::PacketSerializer<LobbyJoinPacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<LobbyJoinPacket> p, std::shared_ptr<znet::Buffer> b) override {
		b->WriteInt<uint32_t>(p->senderId);
		b->WriteString(p->playerName);
		b->WriteString(p->password);
		return b;
	}
	std::shared_ptr<LobbyJoinPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
		auto p = std::make_shared<LobbyJoinPacket>();
		p->senderId = b->ReadInt<uint32_t>();
		p->playerName = b->ReadString();
		p->password = b->ReadString();
		return p;
	}
};

class LobbyStatePacket : public znet::Packet {
public:
	LobbyStatePacket() : Packet(PACKET_LOBBY_STATE) {}
	bool isGlobalServer = false;
	std::string roomCode = "";
	std::vector<uint32_t> playerIds;
	std::vector<std::string> playerNames;
	std::vector<uint8_t> playerTeams;
	std::vector<uint8_t> playerReadys; // 1 for true, 0 for false
};

class LobbyStateSerializer : public znet::PacketSerializer<LobbyStatePacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<LobbyStatePacket> p, std::shared_ptr<znet::Buffer> b) override {
		b->WriteBool(p->isGlobalServer);
		b->WriteString(p->roomCode);
		b->WriteVarInt(p->playerIds.size());
		for (size_t i = 0; i < p->playerIds.size(); i++) {
			b->WriteInt<uint32_t>(p->playerIds[i]);
			b->WriteString(p->playerNames[i]);
			b->WriteInt<uint8_t>(p->playerTeams[i]);
			b->WriteInt<uint8_t>(p->playerReadys[i]);
		}
		return b;
	}
	std::shared_ptr<LobbyStatePacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
		auto p = std::make_shared<LobbyStatePacket>();
		p->isGlobalServer = b->ReadBool();
		p->roomCode = b->ReadString();
		size_t count = b->ReadVarInt<size_t>();
		// Six bytes an entry at the very least, so a bigger count is corrupt.
		if (count > b->readable_bytes()) return p;
		for (size_t i = 0; i < count; i++) {
			p->playerIds.push_back(b->ReadInt<uint32_t>());
			p->playerNames.push_back(b->ReadString());
			p->playerTeams.push_back(b->ReadInt<uint8_t>());
			p->playerReadys.push_back(b->ReadInt<uint8_t>());
		}
		return p;
	}
};

class ToggleReadyPacket : public znet::Packet {
public:
	ToggleReadyPacket() : Packet(PACKET_TOGGLE_READY) {}
	uint32_t senderId = 0;
};

class ToggleReadySerializer : public znet::PacketSerializer<ToggleReadyPacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<ToggleReadyPacket> p, std::shared_ptr<znet::Buffer> b) override {
		b->WriteInt<uint32_t>(p->senderId);
		return b;
	}
	std::shared_ptr<ToggleReadyPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
		auto p = std::make_shared<ToggleReadyPacket>();
		p->senderId = b->ReadInt<uint32_t>();
		return p;
	}
};

class SwitchTeamPacket : public znet::Packet {
public:
	SwitchTeamPacket() : Packet(PACKET_SWITCH_TEAM) {}
	uint32_t senderId = 0;
	uint8_t teamId = 0;
};

class SwitchTeamSerializer : public znet::PacketSerializer<SwitchTeamPacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<SwitchTeamPacket> p, std::shared_ptr<znet::Buffer> b) override {
		b->WriteInt<uint32_t>(p->senderId);
		b->WriteInt<uint8_t>(p->teamId);
		return b;
	}
	std::shared_ptr<SwitchTeamPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
		auto p = std::make_shared<SwitchTeamPacket>();
		p->senderId = b->ReadInt<uint32_t>();
		p->teamId = b->ReadInt<uint8_t>();
		return p;
	}
};

class StartMatchPacket : public znet::Packet {
public:
	StartMatchPacket() : Packet(PACKET_START_MATCH) {}
};

class StartMatchSerializer : public znet::PacketSerializer<StartMatchPacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<StartMatchPacket> p, std::shared_ptr<znet::Buffer> b) override {
		return b;
	}
	std::shared_ptr<StartMatchPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
		return std::make_shared<StartMatchPacket>();
	}
};

class LobbyKickPacket : public znet::Packet {
public:
    LobbyKickPacket() : Packet(PACKET_LOBBY_KICK) {}
    std::string reason = "";
};

class LobbyKickSerializer : public znet::PacketSerializer<LobbyKickPacket> {
public:
    std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<LobbyKickPacket> p, std::shared_ptr<znet::Buffer> b) override {
        b->WriteString(p->reason);
        return b;
    }
    std::shared_ptr<LobbyKickPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> b) override {
        auto p = std::make_shared<LobbyKickPacket>();
        p->reason = b->ReadString();
        return p;
    }
};

#endif //GAMEPACKETS_H
