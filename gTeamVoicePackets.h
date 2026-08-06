/*
 * gTeamVoicePackets.h
 *
 * Versioned wire packets used by network team voice.
 */

#ifndef GTEAMVOICEPACKETS_H_
#define GTEAMVOICEPACKETS_H_

#include "audio/gVoiceConstants.h"

#include <cstring>

#include "znet/codec.h"
#include "znet/packet.h"
#include "znet/packet_serializer.h"
#include "znet/send_options.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>


constexpr znet::PacketId G_TEAM_VOICE_SESSION_PACKET_ID = 0x47564f4943450001ULL;
constexpr znet::PacketId G_TEAM_VOICE_UPLINK_PACKET_ID = 0x47564f4943450002ULL;
constexpr znet::PacketId G_TEAM_VOICE_DOWNLINK_PACKET_ID = 0x47564f4943450003ULL;

constexpr std::uint8_t G_TEAM_VOICE_PROTOCOL_VERSION = 1;
constexpr std::uint8_t G_TEAM_VOICE_CONTROL_CHANNEL = 1;
constexpr std::uint8_t G_TEAM_VOICE_DATA_CHANNEL = 2;

enum gTeamVoicePacketFlags : std::uint8_t {
	G_TEAM_VOICE_FLAG_NONE = 0,
	G_TEAM_VOICE_FLAG_STREAM_START = 1 << 0,
	G_TEAM_VOICE_FLAG_DISCONTINUITY = 1 << 1
};

enum class gTeamVoicePacketError {
	NONE,
	TRUNCATED,
	TRAILING_DATA,
	INVALID_VERSION,
	INVALID_FLAGS,
	INVALID_SESSION,
	INVALID_GENERATION,
	INVALID_SENDER,
	EMPTY_PAYLOAD,
	OVERSIZED_PAYLOAD,
	BUFFER_ERROR
};

using gTeamVoicePacketErrorCallback = std::function<void(gTeamVoicePacketError)>;

class gTeamVoiceSessionPacket : public znet::Packet {
public:
	gTeamVoiceSessionPacket();

	std::uint8_t version = G_TEAM_VOICE_PROTOCOL_VERSION;
	bool enabled = false;
	std::uint64_t sessionid = 0;
	std::uint32_t membershipgeneration = 0;
	std::uint64_t playerid = 0;
};

class gTeamVoiceUplinkPacket : public znet::Packet {
public:
	gTeamVoiceUplinkPacket();

	std::uint8_t version = G_TEAM_VOICE_PROTOCOL_VERSION;
	std::uint8_t flags = G_TEAM_VOICE_FLAG_NONE;
	std::uint64_t sessionid = 0;
	std::uint32_t membershipgeneration = 0;
	std::uint32_t streamgeneration = 0;
	std::uint32_t sequence = 0;
	std::uint64_t sampleposition = 0;
	std::vector<unsigned char> payload;
};

class gTeamVoiceDownlinkPacket : public znet::Packet {
public:
	gTeamVoiceDownlinkPacket();

	std::uint8_t version = G_TEAM_VOICE_PROTOCOL_VERSION;
	std::uint8_t flags = G_TEAM_VOICE_FLAG_NONE;
	std::uint64_t sessionid = 0;
	std::uint32_t recipientgeneration = 0;
	std::uint64_t speakerid = 0;
	std::uint32_t speakergeneration = 0;
	std::uint32_t streamgeneration = 0;
	std::uint32_t sequence = 0;
	std::uint64_t sampleposition = 0;
	std::vector<unsigned char> payload;
};

class gTeamVoiceSessionSerializer : public znet::PacketSerializer<gTeamVoiceSessionPacket> {
public:
	explicit gTeamVoiceSessionSerializer(gTeamVoicePacketErrorCallback errorcallback = {});

	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gTeamVoiceSessionPacket> packet,
			std::shared_ptr<znet::Buffer> buffer) override;
	std::shared_ptr<gTeamVoiceSessionPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> buffer) override;

private:
	void report(gTeamVoicePacketError error) const;

	gTeamVoicePacketErrorCallback errorcallback;
};

class gTeamVoiceUplinkSerializer : public znet::PacketSerializer<gTeamVoiceUplinkPacket> {
public:
	explicit gTeamVoiceUplinkSerializer(gTeamVoicePacketErrorCallback errorcallback = {});

	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gTeamVoiceUplinkPacket> packet,
			std::shared_ptr<znet::Buffer> buffer) override;
	std::shared_ptr<gTeamVoiceUplinkPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> buffer) override;

private:
	void report(gTeamVoicePacketError error) const;

	gTeamVoicePacketErrorCallback errorcallback;
};

class gTeamVoiceDownlinkSerializer : public znet::PacketSerializer<gTeamVoiceDownlinkPacket> {
public:
	explicit gTeamVoiceDownlinkSerializer(gTeamVoicePacketErrorCallback errorcallback = {});

	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<gTeamVoiceDownlinkPacket> packet,
			std::shared_ptr<znet::Buffer> buffer) override;
	std::shared_ptr<gTeamVoiceDownlinkPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> buffer) override;

private:
	void report(gTeamVoicePacketError error) const;

	gTeamVoicePacketErrorCallback errorcallback;
};

gTeamVoicePacketError gValidateTeamVoiceSessionPacket(const gTeamVoiceSessionPacket& packet);
gTeamVoicePacketError gValidateTeamVoiceUplinkPacket(const gTeamVoiceUplinkPacket& packet);
gTeamVoicePacketError gValidateTeamVoiceDownlinkPacket(const gTeamVoiceDownlinkPacket& packet);

void gRegisterTeamVoicePackets(znet::Codec& codec, gTeamVoicePacketErrorCallback errorcallback = {});

znet::SendOptions gGetTeamVoiceControlSendOptions();
znet::SendOptions gGetTeamVoiceDataSendOptions();

bool gIsTeamVoiceSequenceNewer(std::uint32_t sequence, std::uint32_t reference);
std::uint32_t gTeamVoiceSequenceDistance(std::uint32_t newer, std::uint32_t older);

const char* gGetTeamVoicePacketErrorString(gTeamVoicePacketError error);

#endif /* GTEAMVOICEPACKETS_H_ */
