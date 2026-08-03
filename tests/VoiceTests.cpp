#include "audio/gVoiceAudioProcessor.h"
#include "audio/gVoiceConstants.h"
#include "voice/gTeamVoicePackets.h"
#include "voice/gTeamVoiceServer.h"

#include "znet/client.h"
#include "znet/client_events.h"
#include "znet/event.h"
#include "znet/init.h"
#include "znet/packet_handler.h"
#include "znet/server.h"
#include "znet/server_events.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>


namespace {

using namespace std::chrono_literals;

std::atomic<int> failures{0};

#define CHECK(condition) do { \
	if (!(condition)) { \
		std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #condition "\n"; \
		failures.fetch_add(1, std::memory_order_relaxed); \
	} \
} while (false)

template<typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = 3000ms) {
	auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (predicate()) return true;
		std::this_thread::sleep_for(2ms);
	}
	return predicate();
}

std::array<std::int16_t, gvoice::FRAME_SAMPLES> makeSine(double frequency, double phase = 0.0) {
	std::array<std::int16_t, gvoice::FRAME_SAMPLES> frame{};
	constexpr double PI = 3.14159265358979323846;
	for (std::size_t i = 0; i < frame.size(); i++) {
		frame[i] = static_cast<std::int16_t>(std::sin(phase + 2.0 * PI * frequency * i / gvoice::SAMPLERATE) * 12000.0);
	}
	return frame;
}

gTeamVoiceUplinkPacket makeUplink(std::uint32_t sequence = 0) {
	gTeamVoiceUplinkPacket packet;
	packet.flags = sequence == 0 ? G_TEAM_VOICE_FLAG_STREAM_START : G_TEAM_VOICE_FLAG_NONE;
	packet.sessionid = 100;
	packet.membershipgeneration = 1;
	packet.streamgeneration = 1;
	packet.sequence = sequence;
	packet.sampleposition = static_cast<std::uint64_t>(sequence) * gvoice::FRAME_SAMPLES;
	packet.payload = {1, 2, 3, 4};
	return packet;
}

void testSerialization() {
	gTeamVoiceUplinkPacket source = makeUplink(7);
	gTeamVoiceUplinkSerializer serializer;
	auto buffer = serializer.SerializeTyped(std::make_shared<gTeamVoiceUplinkPacket>(source), std::make_shared<znet::Buffer>());
	CHECK(buffer != nullptr);
	auto decoded = serializer.DeserializeTyped(buffer);
	CHECK(decoded != nullptr);
	if (decoded) {
		CHECK(decoded->sessionid == source.sessionid);
		CHECK(decoded->sequence == source.sequence);
		CHECK(decoded->sampleposition == source.sampleposition);
		CHECK(decoded->payload == source.payload);
	}

	gTeamVoiceSessionPacket sessionpacket;
	sessionpacket.enabled = true;
	sessionpacket.sessionid = 100;
	sessionpacket.membershipgeneration = 4;
	sessionpacket.playerid = 55;
	gTeamVoiceSessionSerializer sessionserializer;
	auto sessionbuffer = sessionserializer.SerializeTyped(std::make_shared<gTeamVoiceSessionPacket>(sessionpacket),
			std::make_shared<znet::Buffer>());
	auto decodedsession = sessionserializer.DeserializeTyped(sessionbuffer);
	CHECK(decodedsession != nullptr);
	if (decodedsession) {
		CHECK(decodedsession->enabled);
		CHECK(decodedsession->membershipgeneration == 4);
		CHECK(decodedsession->playerid == 55);
	}
	sessionpacket.membershipgeneration = 0;
	CHECK(gValidateTeamVoiceSessionPacket(sessionpacket) == gTeamVoicePacketError::INVALID_GENERATION);

	gTeamVoiceDownlinkPacket downlink;
	downlink.flags = G_TEAM_VOICE_FLAG_STREAM_START;
	downlink.sessionid = 100;
	downlink.recipientgeneration = 4;
	downlink.speakerid = 55;
	downlink.speakergeneration = 3;
	downlink.streamgeneration = 2;
	downlink.sequence = 9;
	downlink.sampleposition = 8640;
	downlink.payload.resize(gvoice::NETWORK_MAX_OPUS_BYTES, 0x5a);
	gTeamVoiceDownlinkSerializer downlinkserializer;
	auto downlinkbuffer = downlinkserializer.SerializeTyped(std::make_shared<gTeamVoiceDownlinkPacket>(downlink),
			std::make_shared<znet::Buffer>());
	auto decodeddownlink = downlinkserializer.DeserializeTyped(downlinkbuffer);
	CHECK(decodeddownlink != nullptr);
	if (decodeddownlink) {
		CHECK(decodeddownlink->speakerid == downlink.speakerid);
		CHECK(decodeddownlink->sequence == downlink.sequence);
		CHECK(decodeddownlink->payload == downlink.payload);
	}
	downlink.flags = 0x80;
	CHECK(gValidateTeamVoiceDownlinkPacket(downlink) == gTeamVoicePacketError::INVALID_FLAGS);
	auto shortdownlink = std::make_shared<znet::Buffer>();
	shortdownlink->WriteInt<std::uint8_t>(G_TEAM_VOICE_PROTOCOL_VERSION);
	CHECK(downlinkserializer.DeserializeTyped(shortdownlink) == nullptr);

	gTeamVoiceUplinkPacket empty = source;
	empty.payload.clear();
	CHECK(gValidateTeamVoiceUplinkPacket(empty) == gTeamVoicePacketError::EMPTY_PAYLOAD);

	gTeamVoiceUplinkPacket oversized = source;
	oversized.payload.resize(gvoice::NETWORK_MAX_OPUS_BYTES + 1);
	CHECK(gValidateTeamVoiceUplinkPacket(oversized) == gTeamVoicePacketError::OVERSIZED_PAYLOAD);

	gTeamVoiceUplinkPacket invalidversion = source;
	invalidversion.version++;
	CHECK(gValidateTeamVoiceUplinkPacket(invalidversion) == gTeamVoicePacketError::INVALID_VERSION);

	gTeamVoiceUplinkPacket invalidsession = source;
	invalidsession.sessionid = 0;
	CHECK(gValidateTeamVoiceUplinkPacket(invalidsession) == gTeamVoicePacketError::INVALID_SESSION);

	gTeamVoiceDownlinkPacket invalidsender;
	invalidsender.sessionid = 100;
	invalidsender.recipientgeneration = 1;
	invalidsender.speakergeneration = 1;
	invalidsender.streamgeneration = 1;
	invalidsender.payload = {1};
	CHECK(gValidateTeamVoiceDownlinkPacket(invalidsender) == gTeamVoicePacketError::INVALID_SENDER);

	auto truncated = std::make_shared<znet::Buffer>();
	truncated->WriteInt<std::uint8_t>(G_TEAM_VOICE_PROTOCOL_VERSION);
	CHECK(serializer.DeserializeTyped(truncated) == nullptr);

	auto trailing = serializer.SerializeTyped(std::make_shared<gTeamVoiceUplinkPacket>(source), std::make_shared<znet::Buffer>());
	trailing->WriteInt<std::uint8_t>(99);
	trailing->set_read_cursor(0);
	CHECK(serializer.DeserializeTyped(trailing) == nullptr);

	CHECK(gIsTeamVoiceSequenceNewer(0, UINT32_MAX));
	CHECK(gTeamVoiceSequenceDistance(0, UINT32_MAX) == 1);
	CHECK(!gIsTeamVoiceSequenceNewer(UINT32_MAX, 0));
}

struct Sink {
	std::vector<std::shared_ptr<znet::Packet>> packets;
};

void testRouting() {
	gTeamVoiceServer::Config config;
	config.packetspersecond = 1000.0;
	config.packetburst = 1000.0;
	config.bytespersecond = 1000000.0;
	config.byteburst = 1000000.0;
	gTeamVoiceServer server(config);
	std::array<Sink, 4> sinks;
	for (std::size_t i = 0; i < sinks.size(); i++) {
		CHECK(server.addPeer(i + 1, [&, i](const std::shared_ptr<znet::Packet>& packet, const SendOptions&) {
			sinks[i].packets.push_back(packet);
			return true;
		}));
	}
	CHECK(server.setPeerState(1, {101, 1, 500, true, true}));
	CHECK(server.setPeerState(2, {102, 1, 500, true, true}));
	CHECK(server.setPeerState(3, {103, 2, 500, true, true}));
	CHECK(server.setPeerState(4, {104, 2, 500, true, true}));
	for (Sink& sink : sinks) sink.packets.clear();

	gTeamVoiceUplinkPacket froma = makeUplink();
	froma.sessionid = 500;
	server.handleVoicePacket(1, froma);
	CHECK(sinks[0].packets.empty());
	CHECK(sinks[1].packets.size() == 1);
	CHECK(sinks[2].packets.empty());
	CHECK(sinks[3].packets.empty());
	if (!sinks[1].packets.empty()) {
		auto relay = std::dynamic_pointer_cast<gTeamVoiceDownlinkPacket>(sinks[1].packets.front());
		CHECK(relay != nullptr);
		if (relay) CHECK(relay->speakerid == 101);
	}

	for (Sink& sink : sinks) sink.packets.clear();
	gTeamVoiceUplinkPacket fromc = froma;
	server.handleVoicePacket(3, fromc);
	CHECK(sinks[0].packets.empty());
	CHECK(sinks[1].packets.empty());
	CHECK(sinks[2].packets.empty());
	CHECK(sinks[3].packets.size() == 1);

	CHECK(server.clearPeerState(1));
	sinks[0].packets.clear();
	server.handleVoicePacket(1, froma);
	CHECK(server.getStats().routingrejections >= 1);

	server.removePeer(4);
	for (Sink& sink : sinks) sink.packets.clear();
	server.handleVoicePacket(3, fromc);
	CHECK(sinks[3].packets.empty());

	CHECK(server.setPeerState(2, {102, 2, 500, true, true}));
	for (Sink& sink : sinks) sink.packets.clear();
	server.handleVoicePacket(3, fromc);
	CHECK(sinks[1].packets.size() == 1);

	gTeamVoiceUplinkPacket stale = fromc;
	stale.sessionid = 499;
	server.handleVoicePacket(3, stale);
	CHECK(server.getStats().routingrejections >= 2);

	// There is deliberately no sender or team field in the uplink packet. Only
	// setPeerState(), which is server-owned, can alter routing.
	CHECK(server.getStats().relayedpackets == 3);

	CHECK(server.setPeerState(3, {103, 2, 500, false, true}));
	server.handleVoicePacket(3, fromc);
	CHECK(server.getStats().routingrejections >= 3);

	gTeamVoiceServer::Config limitedconfig;
	limitedconfig.packetburst = 1.0;
	limitedconfig.packetspersecond = 0.0;
	limitedconfig.byteburst = 1024.0;
	limitedconfig.bytespersecond = 0.0;
	gTeamVoiceServer limited(limitedconfig);
	CHECK(limited.addPeer(1, [](const std::shared_ptr<znet::Packet>&, const SendOptions&) { return true; }));
	CHECK(limited.addPeer(2, [](const std::shared_ptr<znet::Packet>&, const SendOptions&) { return true; }));
	CHECK(limited.setPeerState(1, {101, 1, 500, true, true}));
	CHECK(limited.setPeerState(2, {102, 1, 500, true, true}));
	limited.handleVoicePacket(1, froma);
	limited.handleVoicePacket(1, froma);
	CHECK(limited.getStats().relayedpackets == 1);
	CHECK(limited.getStats().ratelimitedpackets == 1);

	bool failcontrol = true;
	std::vector<std::uint32_t> controlgenerations;
	gTeamVoiceServer retriable;
	CHECK(retriable.addPeer(1, [&](const std::shared_ptr<znet::Packet>& packet, const SendOptions&) {
		auto control = std::dynamic_pointer_cast<gTeamVoiceSessionPacket>(packet);
		if (control && control->enabled) controlgenerations.push_back(control->membershipgeneration);
		return !failcontrol;
	}));
	CHECK(!retriable.setPeerState(1, {101, 1, 500, true, true}));
	CHECK(controlgenerations.size() == 1);
	failcontrol = false;
	CHECK(retriable.setPeerState(1, {101, 1, 500, true, true}));
	CHECK(controlgenerations.size() == 2);
	if (controlgenerations.size() == 2) CHECK(controlgenerations[0] == controlgenerations[1]);
}

std::vector<gTeamVoiceUplinkPacket> encodeFrames(gVoiceAudioProcessor& sender, int count, double frequency) {
	std::vector<gTeamVoiceUplinkPacket> packets;
	sender.setSession(700, 1, 201);
	sender.setTransmitting(true);
	std::this_thread::sleep_for(10ms);
	for (int i = 0; i < count; i++) {
		auto frame = makeSine(frequency, i * 0.1);
		sender.pushCapturedSamples(frame.data(), frame.size());
	}
	waitFor([&]() { return sender.getStats().encodedpackets >= static_cast<std::uint64_t>(count); });
	gTeamVoiceUplinkPacket packet;
	while (sender.popOutgoingPacket(packet)) {
		packets.push_back(packet);
		packet = gTeamVoiceUplinkPacket();
	}
	return packets;
}

gTeamVoiceDownlinkPacket makeDownlink(const gTeamVoiceUplinkPacket& uplink, std::uint64_t speakerid = 201) {
	gTeamVoiceDownlinkPacket downlink;
	downlink.flags = uplink.flags;
	downlink.sessionid = uplink.sessionid;
	downlink.recipientgeneration = 1;
	downlink.speakerid = speakerid;
	downlink.speakergeneration = 1;
	downlink.streamgeneration = uplink.streamgeneration;
	downlink.sequence = uplink.sequence;
	downlink.sampleposition = uplink.sampleposition;
	downlink.payload = uplink.payload;
	return downlink;
}

void testAudioPipeline() {
	gVoiceAudioProcessor::Config config;
	config.initialjitterpackets = 2;
	config.outgoingpackets = 16;
	gVoiceAudioProcessor sender(config);
	gVoiceAudioProcessor receiver(config);
	CHECK(sender.start());
	CHECK(receiver.start());
	receiver.setSession(700, 1, 202);
	auto packets = encodeFrames(sender, 6, 440.0);
	CHECK(packets.size() == 6);
	if (packets.size() < 4) {
		sender.stop();
		receiver.stop();
		return;
	}
	for (const auto& packet : packets) CHECK(receiver.pushIncomingPacket(makeDownlink(packet)));
	CHECK(waitFor([&]() { return receiver.getStats().decodedpackets >= 2; }));
	std::array<std::int16_t, gvoice::FRAME_SAMPLES> output{};
	CHECK(waitFor([&]() { return receiver.popPlaybackSamples(output.data(), output.size()) == output.size(); }));
	CHECK(std::any_of(output.begin(), output.end(), [](std::int16_t sample) { return sample != 0; }));
	CHECK(receiver.getStats().decodeerrors == 0);

	gVoiceAudioProcessor reorderreceiver(config);
	CHECK(reorderreceiver.start());
	reorderreceiver.setSession(700, 1, 203);
	CHECK(reorderreceiver.pushIncomingPacket(makeDownlink(packets[2])));
	CHECK(reorderreceiver.pushIncomingPacket(makeDownlink(packets[0])));
	CHECK(reorderreceiver.pushIncomingPacket(makeDownlink(packets[0])));
	CHECK(reorderreceiver.pushIncomingPacket(makeDownlink(packets[1])));
	CHECK(waitFor([&]() { return reorderreceiver.getStats().duplicatepackets >= 1; }));
	CHECK(reorderreceiver.getStats().reorderedpackets >= 1);
	auto stalereset = makeDownlink(packets[0]);
	stalereset.flags |= G_TEAM_VOICE_FLAG_DISCONTINUITY;
	CHECK(reorderreceiver.pushIncomingPacket(stalereset));
	CHECK(waitFor([&]() { return reorderreceiver.getStats().latepackets >= 1; }));

	gVoiceAudioProcessor lossreceiver(config);
	CHECK(lossreceiver.start());
	lossreceiver.setSession(700, 1, 204);
	CHECK(lossreceiver.pushIncomingPacket(makeDownlink(packets[0])));
	CHECK(lossreceiver.pushIncomingPacket(makeDownlink(packets[2])));
	CHECK(lossreceiver.pushIncomingPacket(makeDownlink(packets[3])));
	CHECK(waitFor([&]() { return lossreceiver.getStats().plcframes >= 1; }));
	CHECK(lossreceiver.getStats().missingpackets >= 1);

	gVoiceAudioProcessor generationreceiver(config);
	CHECK(generationreceiver.start());
	generationreceiver.setSession(700, 1, 205);
	gTeamVoiceDownlinkPacket stalegeneration = makeDownlink(packets[0]);
	CHECK(generationreceiver.pushIncomingPacket(stalegeneration));
	generationreceiver.setSession(700, 2, 205);
	CHECK(!generationreceiver.pushIncomingPacket(stalegeneration));
	stalegeneration.recipientgeneration = 2;
	CHECK(generationreceiver.pushIncomingPacket(stalegeneration));
	generationreceiver.setSession(701, 3, 205);
	stalegeneration.recipientgeneration = 3;
	CHECK(!generationreceiver.pushIncomingPacket(stalegeneration));

	gVoiceAudioProcessor::Config overflowconfig;
	overflowconfig.captureframes = 16;
	overflowconfig.outgoingpackets = 2;
	gVoiceAudioProcessor overflowprocessor(overflowconfig);
	CHECK(overflowprocessor.start());
	overflowprocessor.setSession(800, 1, 206);
	overflowprocessor.setTransmitting(true);
	std::this_thread::sleep_for(10ms);
	for (int i = 0; i < 8; i++) {
		auto frame = makeSine(330.0, i * 0.1);
		overflowprocessor.pushCapturedSamples(frame.data(), frame.size());
	}
	CHECK(waitFor([&]() { return overflowprocessor.getStats().encodedpackets >= 8; }));
	CHECK(overflowprocessor.getStats().outgoingqueuedrops >= 6);
	std::vector<gTeamVoiceUplinkPacket> retained;
	gTeamVoiceUplinkPacket retainedpacket;
	while (overflowprocessor.popOutgoingPacket(retainedpacket)) {
		retained.push_back(retainedpacket);
		retainedpacket = gTeamVoiceUplinkPacket();
	}
	CHECK(retained.size() == 2);
	if (!retained.empty()) CHECK((retained.front().flags & G_TEAM_VOICE_FLAG_DISCONTINUITY) != 0);

	gVoiceAudioProcessor::Config zeroconfig;
	zeroconfig.captureframes = 0;
	zeroconfig.outgoingpackets = 0;
	zeroconfig.incomingpackets = 0;
	zeroconfig.playbackframes = 0;
	zeroconfig.maxspeakers = 0;
	zeroconfig.jitterpackets = 0;
	zeroconfig.initialjitterpackets = 0;
	gVoiceAudioProcessor zeroprocessor(zeroconfig);
	CHECK(zeroprocessor.start());
	zeroprocessor.setSession(700, 1, 207);
	CHECK(zeroprocessor.pushIncomingPacket(makeDownlink(packets[0])));
	zeroprocessor.stop();
	CHECK(!zeroprocessor.pushIncomingPacket(makeDownlink(packets[0])));

	std::array<std::int16_t, 4> maximum = {32767, 32767, 32767, 32767};
	std::array<std::int16_t, 4> minimum = {-32768, -32768, -32768, -32768};
	std::array<std::int16_t, 4> mixed{};
	std::vector<const std::int16_t*> inputs = {maximum.data(), maximum.data()};
	gVoiceAudioProcessor::mixFrames(inputs, mixed.size(), mixed.data());
	CHECK(std::all_of(mixed.begin(), mixed.end(), [](std::int16_t sample) { return sample == 32767; }));
	inputs = {maximum.data(), minimum.data()};
	gVoiceAudioProcessor::mixFrames(inputs, mixed.size(), mixed.data());
	CHECK(std::all_of(mixed.begin(), mixed.end(), [](std::int16_t sample) { return sample == 0; }));

	sender.stop();
	sender.stop();
	CHECK(sender.start());
	auto restartframe = makeSine(550.0);
	sender.pushCapturedSamples(restartframe.data(), restartframe.size());
	CHECK(waitFor([&]() { return sender.getStats().encodedpackets >= 7; }));
	gTeamVoiceUplinkPacket restartedpacket;
	CHECK(sender.popOutgoingPacket(restartedpacket));
	CHECK(gIsTeamVoiceSequenceNewer(restartedpacket.streamgeneration, packets.back().streamgeneration));
	sender.pushCapturedSamples(restartframe.data(), restartframe.size());
	CHECK(waitFor([&]() { return sender.getStats().encodedpackets >= 8; }));
	sender.setEnabled(false);
	CHECK(!sender.popOutgoingPacket(restartedpacket));
	CHECK(!sender.pushIncomingPacket(makeDownlink(packets[0])));
	sender.stop();
	receiver.stop();
	CHECK(!receiver.pushIncomingPacket(makeDownlink(packets[0])));
	reorderreceiver.stop();
	lossreceiver.stop();
	generationreceiver.stop();
	overflowprocessor.stop();
}

class ServerVoiceHandler : public znet::PacketHandler<ServerVoiceHandler, gTeamVoiceUplinkPacket> {
public:
	ServerVoiceHandler(gTeamVoiceServer& router, std::uint64_t connectionid)
			: router(router), connectionid(connectionid) {
	}

	void OnPacket(std::shared_ptr<gTeamVoiceUplinkPacket> packet) {
		router.handleVoicePacket(connectionid, *packet);
	}

private:
	gTeamVoiceServer& router;
	std::uint64_t connectionid;
};

struct NetworkClient;

class ClientVoiceHandler : public znet::PacketHandler<ClientVoiceHandler, gTeamVoiceSessionPacket, gTeamVoiceDownlinkPacket> {
public:
	explicit ClientVoiceHandler(NetworkClient& owner) : owner(owner) {
	}

	void OnPacket(std::shared_ptr<gTeamVoiceSessionPacket> packet);
	void OnPacket(std::shared_ptr<gTeamVoiceDownlinkPacket> packet);

private:
	NetworkClient& owner;
};

struct NetworkClient {
	NetworkClient(std::uint64_t playerid, std::uint16_t port)
			: playerid(playerid), processor(makeProcessorConfig()),
			  client(znet::ClientConfig{"127.0.0.1", port, std::chrono::seconds(10), znet::ConnectionType::ZDT}) {
		client.SetEventCallback([this](znet::Event& event) {
			znet::EventDispatcher dispatcher{event};
			dispatcher.Dispatch<znet::ClientConnectedToServerEvent>([this](znet::ClientConnectedToServerEvent& connected) {
				auto codec = std::make_shared<znet::Codec>();
				gRegisterTeamVoicePackets(*codec);
				auto connectedSession = connected.session();
				connectedSession->SetCodec(codec);
				connectedSession->SetHandler(std::make_shared<ClientVoiceHandler>(*this));
				{
					std::lock_guard<std::mutex> lock(mutex);
					session = connectedSession;
				}
				isconnected.store(true, std::memory_order_release);
				return false;
			});
			dispatcher.Dispatch<znet::ClientDisconnectedFromServerEvent>([this](znet::ClientDisconnectedFromServerEvent&) {
				isconnected.store(false, std::memory_order_release);
				return false;
			});
		});
	}

	static gVoiceAudioProcessor::Config makeProcessorConfig() {
		gVoiceAudioProcessor::Config config;
		config.initialjitterpackets = 2;
		config.outgoingpackets = 32;
		return config;
	}

	bool start() {
		if (!processor.start()) return false;
		if (client.Bind() != znet::Result::Success) return false;
		return client.Connect() == znet::Result::Success;
	}

	std::shared_ptr<znet::PeerSession> getSession() {
		std::lock_guard<std::mutex> lock(mutex);
		return session;
	}

	std::size_t sendAudio(double frequency) {
		processor.setTransmitting(true);
		std::this_thread::sleep_for(10ms);
		for (int i = 0; i < 8; i++) {
			auto frame = makeSine(frequency, i * 0.1);
			processor.pushCapturedSamples(frame.data(), frame.size());
		}
		waitFor([this]() { return processor.getStats().encodedpackets >= 8; });
		auto activeSession = getSession();
		gTeamVoiceUplinkPacket packet;
		std::size_t sent = 0;
		while (activeSession && processor.popOutgoingPacket(packet)) {
			if (activeSession->SendPacket(std::make_shared<gTeamVoiceUplinkPacket>(packet), gGetTeamVoiceDataSendOptions())) sent++;
			packet = gTeamVoiceUplinkPacket();
			std::this_thread::sleep_for(std::chrono::milliseconds(gvoice::FRAME_MILLISECONDS));
		}
		return sent;
	}

	void stop() {
		client.Disconnect();
		client.Wait();
		processor.stop();
	}

	std::uint64_t playerid;
	gVoiceAudioProcessor processor;
	znet::Client client;
	std::mutex mutex;
	std::shared_ptr<znet::PeerSession> session;
	std::atomic<bool> isconnected{false};
	std::atomic<std::uint64_t> downlinks{0};
	std::atomic<std::uint64_t> controls{0};
};

void ClientVoiceHandler::OnPacket(std::shared_ptr<gTeamVoiceSessionPacket> packet) {
	owner.controls.fetch_add(1, std::memory_order_relaxed);
	if (packet->enabled) {
		owner.processor.setSession(packet->sessionid, packet->membershipgeneration, packet->playerid);
	} else {
		owner.processor.clearSession();
	}
}

void ClientVoiceHandler::OnPacket(std::shared_ptr<gTeamVoiceDownlinkPacket> packet) {
	owner.downlinks.fetch_add(1, std::memory_order_relaxed);
	owner.processor.pushIncomingPacket(*packet);
}

void testNetworkSmoke() {
	CHECK(znet::Init() == znet::Result::Success);
	gTeamVoiceServer::Config routerconfig;
	routerconfig.packetspersecond = 1000.0;
	routerconfig.packetburst = 1000.0;
	routerconfig.bytespersecond = 1000000.0;
	routerconfig.byteburst = 1000000.0;
	gTeamVoiceServer router(routerconfig);
	std::mutex servermutex;
	std::vector<std::shared_ptr<znet::PeerSession>> serversessions;

	znet::ServerConfig serverconfig{"127.0.0.1", ZNET_PORT_AUTO, std::chrono::seconds(10), znet::ConnectionType::ZDT};
	znet::Server server(serverconfig);
	server.SetEventCallback([&](znet::Event& event) {
		znet::EventDispatcher dispatcher{event};
		dispatcher.Dispatch<znet::IncomingClientConnectedEvent>([&](znet::IncomingClientConnectedEvent& connected) {
			auto session = connected.session();
			auto codec = std::make_shared<znet::Codec>();
			gRegisterTeamVoicePackets(*codec, [&](gTeamVoicePacketError error) { router.reportMalformedPacket(error); });
			session->SetCodec(codec);
			session->SetHandler(std::make_shared<ServerVoiceHandler>(router, session->id()));
			CHECK(router.addPeer(session));
			std::lock_guard<std::mutex> lock(servermutex);
			serversessions.push_back(session);
			return false;
		});
		dispatcher.Dispatch<znet::ServerClientDisconnectedEvent>([&](znet::ServerClientDisconnectedEvent& disconnected) {
			router.removePeer(disconnected.session()->id());
			return false;
		});
	});
	CHECK(server.Bind() == znet::Result::Success);
	CHECK(server.Listen() == znet::Result::Success);
	auto address = server.bind_address();
	CHECK(address != nullptr);
	if (!address) {
		server.Stop();
		server.Wait();
		znet::Cleanup();
		return;
	}

	std::array<std::unique_ptr<NetworkClient>, 4> clients;
	for (std::size_t i = 0; i < clients.size(); i++) {
		clients[i] = std::make_unique<NetworkClient>(301 + i, address->port());
		CHECK(clients[i]->start());
		CHECK(waitFor([&, i]() {
			std::lock_guard<std::mutex> lock(servermutex);
			return clients[i]->isconnected.load(std::memory_order_acquire) && serversessions.size() >= i + 1;
		}, 10000ms));
	}

	{
		std::lock_guard<std::mutex> lock(servermutex);
		CHECK(serversessions.size() == 4);
		if (serversessions.size() == 4) {
			CHECK(router.setPeerState(serversessions[0]->id(), {301, 1, 900, true, true}));
			CHECK(router.setPeerState(serversessions[1]->id(), {302, 1, 900, true, true}));
			CHECK(router.setPeerState(serversessions[2]->id(), {303, 2, 900, true, true}));
			CHECK(router.setPeerState(serversessions[3]->id(), {304, 2, 900, true, true}));
		}
	}
	CHECK(waitFor([&]() {
		return std::all_of(clients.begin(), clients.end(), [](const std::unique_ptr<NetworkClient>& client) {
			return client->controls.load(std::memory_order_relaxed) >= 1;
		});
	}, 10000ms));

	std::size_t asent = clients[0]->sendAudio(440.0);
	CHECK(asent > 0);
	CHECK(waitFor([&]() { return clients[1]->processor.getStats().decodedpackets >= 2; }, 5000ms));
	CHECK(clients[0]->downlinks.load(std::memory_order_relaxed) == 0);
	CHECK(clients[1]->downlinks.load(std::memory_order_relaxed) > 0);
	CHECK(clients[2]->downlinks.load(std::memory_order_relaxed) == 0);
	CHECK(clients[3]->downlinks.load(std::memory_order_relaxed) == 0);

	std::size_t csent = clients[2]->sendAudio(660.0);
	CHECK(csent > 0);
	CHECK(waitFor([&]() { return clients[3]->processor.getStats().decodedpackets >= 2; }, 5000ms));
	CHECK(clients[0]->downlinks.load(std::memory_order_relaxed) == 0);
	CHECK(clients[1]->downlinks.load(std::memory_order_relaxed) > 0);
	CHECK(clients[2]->downlinks.load(std::memory_order_relaxed) == 0);
	CHECK(clients[3]->downlinks.load(std::memory_order_relaxed) > 0);

	auto stats = router.getStats();
	if (clients[3]->downlinks.load(std::memory_order_relaxed) == 0) {
		std::cerr << "network diagnostics: A sent=" << asent << " C sent=" << csent
				<< " server received=" << stats.receivedpackets << " relayed=" << stats.relayedpackets
				<< " rejected=" << stats.routingrejections << " rate-limited=" << stats.ratelimitedpackets
				<< " downlinks=" << clients[0]->downlinks.load() << "," << clients[1]->downlinks.load()
				<< "," << clients[2]->downlinks.load() << "," << clients[3]->downlinks.load() << "\n";
	}
	// Voice uses an unreliable channel, so delivery of every queued packet is
	// not a transport guarantee. Both teams decoded audio above; every packet
	// that reached this two-player-per-team router must have one recipient.
	CHECK(stats.receivedpackets >= 4);
	CHECK(stats.relayedpackets == stats.receivedpackets);
	CHECK(stats.routingrejections == 0);
	CHECK(stats.ratelimitedpackets == 0);
	CHECK(stats.malformedpackets == 0);
	CHECK(stats.sendfailures == 0);

	for (auto& client : clients) client->stop();
	server.Stop();
	server.Wait();
	router.reset();
	znet::Cleanup();
}

}

int main() {
	testSerialization();
	testRouting();
	testAudioPipeline();
	testNetworkSmoke();
	if (failures.load(std::memory_order_relaxed) != 0) {
		std::cerr << failures.load(std::memory_order_relaxed) << " test assertion(s) failed\n";
		return 1;
	}
	std::cout << "All gipMultiplayer team voice tests passed\n";
	return 0;
}
