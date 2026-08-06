/*
 * gTeamVoicePackets.cpp
 */

#include "voice/gTeamVoicePackets.h"

#include "znet/buffer.h"

#include <limits>
#include <utility>


namespace {

constexpr std::size_t SESSION_PACKET_BYTES = 1 + 1 + 8 + 4 + 8;
constexpr std::size_t UPLINK_HEADER_BYTES = 1 + 1 + 8 + 4 + 4 + 4 + 8 + 2;
constexpr std::size_t DOWNLINK_HEADER_BYTES = 1 + 1 + 8 + 4 + 8 + 4 + 4 + 4 + 8 + 2;
constexpr std::uint8_t VALID_VOICE_FLAGS = G_TEAM_VOICE_FLAG_STREAM_START | G_TEAM_VOICE_FLAG_DISCONTINUITY;

gTeamVoicePacketError validatePayload(const std::vector<unsigned char>& payload) {
	if (payload.empty()) return gTeamVoicePacketError::EMPTY_PAYLOAD;
	if (payload.size() > gvoice::NETWORK_MAX_OPUS_BYTES) return gTeamVoicePacketError::OVERSIZED_PAYLOAD;
	return gTeamVoicePacketError::NONE;
}

bool hasBufferError(const std::shared_ptr<znet::Buffer>& buffer) {
	return buffer->GetAndClearLastError() != znet::BufferError::None;
}

}

gTeamVoiceSessionPacket::gTeamVoiceSessionPacket() : Packet(G_TEAM_VOICE_SESSION_PACKET_ID) {
}

gTeamVoiceUplinkPacket::gTeamVoiceUplinkPacket() : Packet(G_TEAM_VOICE_UPLINK_PACKET_ID) {
}

gTeamVoiceDownlinkPacket::gTeamVoiceDownlinkPacket() : Packet(G_TEAM_VOICE_DOWNLINK_PACKET_ID) {
}

gTeamVoicePacketError gValidateTeamVoiceSessionPacket(const gTeamVoiceSessionPacket& packet) {
	if (packet.version != G_TEAM_VOICE_PROTOCOL_VERSION) return gTeamVoicePacketError::INVALID_VERSION;
	if (!packet.enabled) return gTeamVoicePacketError::NONE;
	if (packet.sessionid == 0) return gTeamVoicePacketError::INVALID_SESSION;
	if (packet.membershipgeneration == 0) return gTeamVoicePacketError::INVALID_GENERATION;
	if (packet.playerid == 0) return gTeamVoicePacketError::INVALID_SENDER;
	return gTeamVoicePacketError::NONE;
}

gTeamVoicePacketError gValidateTeamVoiceUplinkPacket(const gTeamVoiceUplinkPacket& packet) {
	if (packet.version != G_TEAM_VOICE_PROTOCOL_VERSION) return gTeamVoicePacketError::INVALID_VERSION;
	if ((packet.flags & ~VALID_VOICE_FLAGS) != 0) return gTeamVoicePacketError::INVALID_FLAGS;
	if (packet.sessionid == 0) return gTeamVoicePacketError::INVALID_SESSION;
	if (packet.membershipgeneration == 0 || packet.streamgeneration == 0) return gTeamVoicePacketError::INVALID_GENERATION;
	return validatePayload(packet.payload);
}

gTeamVoicePacketError gValidateTeamVoiceDownlinkPacket(const gTeamVoiceDownlinkPacket& packet) {
	if (packet.version != G_TEAM_VOICE_PROTOCOL_VERSION) return gTeamVoicePacketError::INVALID_VERSION;
	if ((packet.flags & ~VALID_VOICE_FLAGS) != 0) return gTeamVoicePacketError::INVALID_FLAGS;
	if (packet.sessionid == 0) return gTeamVoicePacketError::INVALID_SESSION;
	if (packet.recipientgeneration == 0 || packet.speakergeneration == 0 || packet.streamgeneration == 0) {
		return gTeamVoicePacketError::INVALID_GENERATION;
	}
	if (packet.speakerid == 0) return gTeamVoicePacketError::INVALID_SENDER;
	return validatePayload(packet.payload);
}

gTeamVoiceSessionSerializer::gTeamVoiceSessionSerializer(gTeamVoicePacketErrorCallback errorcallback)
		: errorcallback(std::move(errorcallback)) {
}

std::shared_ptr<znet::Buffer> gTeamVoiceSessionSerializer::SerializeTyped(
		std::shared_ptr<gTeamVoiceSessionPacket> packet, std::shared_ptr<znet::Buffer> buffer) {
	if (!packet) return nullptr;
	gTeamVoicePacketError error = gValidateTeamVoiceSessionPacket(*packet);
	if (error != gTeamVoicePacketError::NONE) {
		report(error);
		return nullptr;
	}
	buffer->WriteInt<std::uint8_t>(packet->version);
	buffer->WriteInt<std::uint8_t>(packet->enabled ? 1 : 0);
	buffer->WriteInt<std::uint64_t>(packet->sessionid);
	buffer->WriteInt<std::uint32_t>(packet->membershipgeneration);
	buffer->WriteInt<std::uint64_t>(packet->playerid);
	if (hasBufferError(buffer)) {
		report(gTeamVoicePacketError::BUFFER_ERROR);
		return nullptr;
	}
	return buffer;
}

std::shared_ptr<gTeamVoiceSessionPacket> gTeamVoiceSessionSerializer::DeserializeTyped(std::shared_ptr<znet::Buffer> buffer) {
	if (!buffer || buffer->readable_bytes() < SESSION_PACKET_BYTES) {
		report(gTeamVoicePacketError::TRUNCATED);
		return nullptr;
	}
	if (buffer->readable_bytes() > SESSION_PACKET_BYTES) {
		report(gTeamVoicePacketError::TRAILING_DATA);
		return nullptr;
	}
	auto packet = std::make_shared<gTeamVoiceSessionPacket>();
	packet->version = buffer->ReadInt<std::uint8_t>();
	packet->enabled = buffer->ReadInt<std::uint8_t>() != 0;
	packet->sessionid = buffer->ReadInt<std::uint64_t>();
	packet->membershipgeneration = buffer->ReadInt<std::uint32_t>();
	packet->playerid = buffer->ReadInt<std::uint64_t>();
	if (hasBufferError(buffer)) {
		report(gTeamVoicePacketError::BUFFER_ERROR);
		return nullptr;
	}
	gTeamVoicePacketError error = gValidateTeamVoiceSessionPacket(*packet);
	if (error != gTeamVoicePacketError::NONE) {
		report(error);
		return nullptr;
	}
	return packet;
}

void gTeamVoiceSessionSerializer::report(gTeamVoicePacketError error) const {
	if (errorcallback) errorcallback(error);
}

gTeamVoiceUplinkSerializer::gTeamVoiceUplinkSerializer(gTeamVoicePacketErrorCallback errorcallback)
		: errorcallback(std::move(errorcallback)) {
}

std::shared_ptr<znet::Buffer> gTeamVoiceUplinkSerializer::SerializeTyped(
		std::shared_ptr<gTeamVoiceUplinkPacket> packet, std::shared_ptr<znet::Buffer> buffer) {
	if (!packet) return nullptr;
	gTeamVoicePacketError error = gValidateTeamVoiceUplinkPacket(*packet);
	if (error != gTeamVoicePacketError::NONE) {
		report(error);
		return nullptr;
	}
	buffer->WriteInt<std::uint8_t>(packet->version);
	buffer->WriteInt<std::uint8_t>(packet->flags);
	buffer->WriteInt<std::uint64_t>(packet->sessionid);
	buffer->WriteInt<std::uint32_t>(packet->membershipgeneration);
	buffer->WriteInt<std::uint32_t>(packet->streamgeneration);
	buffer->WriteInt<std::uint32_t>(packet->sequence);
	buffer->WriteInt<std::uint64_t>(packet->sampleposition);
	buffer->WriteInt<std::uint16_t>(static_cast<std::uint16_t>(packet->payload.size()));
	buffer->Write(packet->payload.data(), packet->payload.size());
	if (hasBufferError(buffer)) {
		report(gTeamVoicePacketError::BUFFER_ERROR);
		return nullptr;
	}
	return buffer;
}

std::shared_ptr<gTeamVoiceUplinkPacket> gTeamVoiceUplinkSerializer::DeserializeTyped(std::shared_ptr<znet::Buffer> buffer) {
	if (!buffer || buffer->readable_bytes() < UPLINK_HEADER_BYTES) {
		report(gTeamVoicePacketError::TRUNCATED);
		return nullptr;
	}
	auto packet = std::make_shared<gTeamVoiceUplinkPacket>();
	packet->version = buffer->ReadInt<std::uint8_t>();
	packet->flags = buffer->ReadInt<std::uint8_t>();
	packet->sessionid = buffer->ReadInt<std::uint64_t>();
	packet->membershipgeneration = buffer->ReadInt<std::uint32_t>();
	packet->streamgeneration = buffer->ReadInt<std::uint32_t>();
	packet->sequence = buffer->ReadInt<std::uint32_t>();
	packet->sampleposition = buffer->ReadInt<std::uint64_t>();
	std::uint16_t payloadsize = buffer->ReadInt<std::uint16_t>();
	if (hasBufferError(buffer)) {
		report(gTeamVoicePacketError::BUFFER_ERROR);
		return nullptr;
	}
	if (payloadsize == 0) {
		report(gTeamVoicePacketError::EMPTY_PAYLOAD);
		return nullptr;
	}
	if (payloadsize > gvoice::NETWORK_MAX_OPUS_BYTES) {
		report(gTeamVoicePacketError::OVERSIZED_PAYLOAD);
		return nullptr;
	}
	if (buffer->readable_bytes() < payloadsize) {
		report(gTeamVoicePacketError::TRUNCATED);
		return nullptr;
	}
	if (buffer->readable_bytes() > payloadsize) {
		report(gTeamVoicePacketError::TRAILING_DATA);
		return nullptr;
	}
	packet->payload.resize(payloadsize);
	buffer->Read(packet->payload.data(), packet->payload.size());
	if (hasBufferError(buffer)) {
		report(gTeamVoicePacketError::BUFFER_ERROR);
		return nullptr;
	}
	gTeamVoicePacketError error = gValidateTeamVoiceUplinkPacket(*packet);
	if (error != gTeamVoicePacketError::NONE) {
		report(error);
		return nullptr;
	}
	return packet;
}

void gTeamVoiceUplinkSerializer::report(gTeamVoicePacketError error) const {
	if (errorcallback) errorcallback(error);
}

gTeamVoiceDownlinkSerializer::gTeamVoiceDownlinkSerializer(gTeamVoicePacketErrorCallback errorcallback)
		: errorcallback(std::move(errorcallback)) {
}

std::shared_ptr<znet::Buffer> gTeamVoiceDownlinkSerializer::SerializeTyped(
		std::shared_ptr<gTeamVoiceDownlinkPacket> packet, std::shared_ptr<znet::Buffer> buffer) {
	if (!packet) return nullptr;
	gTeamVoicePacketError error = gValidateTeamVoiceDownlinkPacket(*packet);
	if (error != gTeamVoicePacketError::NONE) {
		report(error);
		return nullptr;
	}
	buffer->WriteInt<std::uint8_t>(packet->version);
	buffer->WriteInt<std::uint8_t>(packet->flags);
	buffer->WriteInt<std::uint64_t>(packet->sessionid);
	buffer->WriteInt<std::uint32_t>(packet->recipientgeneration);
	buffer->WriteInt<std::uint64_t>(packet->speakerid);
	buffer->WriteInt<std::uint32_t>(packet->speakergeneration);
	buffer->WriteInt<std::uint32_t>(packet->streamgeneration);
	buffer->WriteInt<std::uint32_t>(packet->sequence);
	buffer->WriteInt<std::uint64_t>(packet->sampleposition);
	buffer->WriteInt<std::uint16_t>(static_cast<std::uint16_t>(packet->payload.size()));
	buffer->Write(packet->payload.data(), packet->payload.size());
	if (hasBufferError(buffer)) {
		report(gTeamVoicePacketError::BUFFER_ERROR);
		return nullptr;
	}
	return buffer;
}

std::shared_ptr<gTeamVoiceDownlinkPacket> gTeamVoiceDownlinkSerializer::DeserializeTyped(std::shared_ptr<znet::Buffer> buffer) {
	if (!buffer || buffer->readable_bytes() < DOWNLINK_HEADER_BYTES) {
		report(gTeamVoicePacketError::TRUNCATED);
		return nullptr;
	}
	auto packet = std::make_shared<gTeamVoiceDownlinkPacket>();
	packet->version = buffer->ReadInt<std::uint8_t>();
	packet->flags = buffer->ReadInt<std::uint8_t>();
	packet->sessionid = buffer->ReadInt<std::uint64_t>();
	packet->recipientgeneration = buffer->ReadInt<std::uint32_t>();
	packet->speakerid = buffer->ReadInt<std::uint64_t>();
	packet->speakergeneration = buffer->ReadInt<std::uint32_t>();
	packet->streamgeneration = buffer->ReadInt<std::uint32_t>();
	packet->sequence = buffer->ReadInt<std::uint32_t>();
	packet->sampleposition = buffer->ReadInt<std::uint64_t>();
	std::uint16_t payloadsize = buffer->ReadInt<std::uint16_t>();
	if (hasBufferError(buffer)) {
		report(gTeamVoicePacketError::BUFFER_ERROR);
		return nullptr;
	}
	if (payloadsize == 0) {
		report(gTeamVoicePacketError::EMPTY_PAYLOAD);
		return nullptr;
	}
	if (payloadsize > gvoice::NETWORK_MAX_OPUS_BYTES) {
		report(gTeamVoicePacketError::OVERSIZED_PAYLOAD);
		return nullptr;
	}
	if (buffer->readable_bytes() < payloadsize) {
		report(gTeamVoicePacketError::TRUNCATED);
		return nullptr;
	}
	if (buffer->readable_bytes() > payloadsize) {
		report(gTeamVoicePacketError::TRAILING_DATA);
		return nullptr;
	}
	packet->payload.resize(payloadsize);
	buffer->Read(packet->payload.data(), packet->payload.size());
	if (hasBufferError(buffer)) {
		report(gTeamVoicePacketError::BUFFER_ERROR);
		return nullptr;
	}
	gTeamVoicePacketError error = gValidateTeamVoiceDownlinkPacket(*packet);
	if (error != gTeamVoicePacketError::NONE) {
		report(error);
		return nullptr;
	}
	return packet;
}

void gTeamVoiceDownlinkSerializer::report(gTeamVoicePacketError error) const {
	if (errorcallback) errorcallback(error);
}

void gRegisterTeamVoicePackets(znet::Codec& codec, gTeamVoicePacketErrorCallback errorcallback) {
	codec.Add(G_TEAM_VOICE_SESSION_PACKET_ID, std::make_unique<gTeamVoiceSessionSerializer>(errorcallback));
	codec.Add(G_TEAM_VOICE_UPLINK_PACKET_ID, std::make_unique<gTeamVoiceUplinkSerializer>(errorcallback));
	codec.Add(G_TEAM_VOICE_DOWNLINK_PACKET_ID, std::make_unique<gTeamVoiceDownlinkSerializer>(std::move(errorcallback)));
}

znet::SendOptions gGetTeamVoiceControlSendOptions() {
    znet::SendOptions options;
    options.Set<znet::ReliableKey>(true);
    options.Set<znet::OrderedKey>(true);
    options.Set<znet::ChannelKey>(G_TEAM_VOICE_CONTROL_CHANNEL);
    return options;
}

znet::SendOptions gGetTeamVoiceDataSendOptions() {
    znet::SendOptions options;
    options.Set<znet::ReliableKey>(false);
    options.Set<znet::OrderedKey>(false);
    options.Set<znet::ChannelKey>(G_TEAM_VOICE_DATA_CHANNEL);
    return options;
}

bool gIsTeamVoiceSequenceNewer(std::uint32_t sequence, std::uint32_t reference) {
	std::uint32_t distance = sequence - reference;
	return distance != 0 && distance < 0x80000000U;
}

std::uint32_t gTeamVoiceSequenceDistance(std::uint32_t newer, std::uint32_t older) {
	return newer - older;
}

const char* gGetTeamVoicePacketErrorString(gTeamVoicePacketError error) {
	switch (error) {
	case gTeamVoicePacketError::NONE: return "none";
	case gTeamVoicePacketError::TRUNCATED: return "truncated packet";
	case gTeamVoicePacketError::TRAILING_DATA: return "trailing packet data";
	case gTeamVoicePacketError::INVALID_VERSION: return "invalid protocol version";
	case gTeamVoicePacketError::INVALID_FLAGS: return "invalid packet flags";
	case gTeamVoicePacketError::INVALID_SESSION: return "invalid session";
	case gTeamVoicePacketError::INVALID_GENERATION: return "invalid generation";
	case gTeamVoicePacketError::INVALID_SENDER: return "invalid sender";
	case gTeamVoicePacketError::EMPTY_PAYLOAD: return "empty Opus payload";
	case gTeamVoicePacketError::OVERSIZED_PAYLOAD: return "oversized Opus payload";
	case gTeamVoicePacketError::BUFFER_ERROR: return "buffer error";
	default: return "unknown packet error";
	}
}
