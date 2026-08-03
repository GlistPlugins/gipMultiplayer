#ifndef VOICEDEMOPACKETS_H_
#define VOICEDEMOPACKETS_H_

#include "gipMultiplayer.h"


constexpr znet::PacketId VOICE_DEMO_READY_PACKET_ID = 0x475644454d4f0001ULL;
constexpr std::uint8_t VOICE_DEMO_PROTOCOL_VERSION = 1;

class VoiceDemoReadyPacket : public znet::Packet {
public:
	VoiceDemoReadyPacket() : Packet(VOICE_DEMO_READY_PACKET_ID) {
	}

	std::uint8_t version = VOICE_DEMO_PROTOCOL_VERSION;
};

class VoiceDemoReadySerializer : public znet::PacketSerializer<VoiceDemoReadyPacket> {
public:
	std::shared_ptr<znet::Buffer> SerializeTyped(std::shared_ptr<VoiceDemoReadyPacket> packet,
			std::shared_ptr<znet::Buffer> buffer) override {
		if (!packet || packet->version != VOICE_DEMO_PROTOCOL_VERSION) return nullptr;
		buffer->WriteInt<std::uint8_t>(packet->version);
		return buffer->GetAndClearLastError() == znet::BufferError::None ? buffer : nullptr;
	}

	std::shared_ptr<VoiceDemoReadyPacket> DeserializeTyped(std::shared_ptr<znet::Buffer> buffer) override {
		if (!buffer || buffer->readable_bytes() != 1) return nullptr;
		auto packet = std::make_shared<VoiceDemoReadyPacket>();
		packet->version = buffer->ReadInt<std::uint8_t>();
		if (buffer->GetAndClearLastError() != znet::BufferError::None ||
				packet->version != VOICE_DEMO_PROTOCOL_VERSION) return nullptr;
		return packet;
	}
};

inline void registerVoiceDemoReadyPacket(znet::Codec& codec) {
	codec.Add(VOICE_DEMO_READY_PACKET_ID, std::make_unique<VoiceDemoReadySerializer>());
}

inline SendOptions getVoiceDemoReadySendOptions() {
	SendOptions options;
	options.Set<ReliableKey>(true);
	options.Set<OrderedKey>(true);
	options.Set<ChannelKey>(0);
	return options;
}

#endif /* VOICEDEMOPACKETS_H_ */
