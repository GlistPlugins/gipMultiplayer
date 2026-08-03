#include "voice/VoiceDemoClient.h"

#include "voice/VoiceDemoPackets.h"

#include "znet/event.h"
#include "znet/packet_handler.h"

#include <chrono>


class VoiceDemoClientPacketHandler : public znet::PacketHandler<VoiceDemoClientPacketHandler,
		gTeamVoiceSessionPacket, gTeamVoiceDownlinkPacket> {
public:
	explicit VoiceDemoClientPacketHandler(VoiceDemoClient* owner) : owner(owner) {
	}

	void OnPacket(std::shared_ptr<gTeamVoiceSessionPacket> packet) {
		owner->handleSessionPacket(*packet);
	}

	void OnPacket(std::shared_ptr<gTeamVoiceDownlinkPacket> packet) {
		owner->handleVoicePacket(*packet);
	}

	void OnUnknown(std::shared_ptr<znet::Packet>) {
	}

private:
	VoiceDemoClient* owner;
};

VoiceDemoClient::VoiceDemoClient() {
}

VoiceDemoClient::~VoiceDemoClient() {
	shutdown();
}

bool VoiceDemoClient::start(const std::string& serverip, std::uint16_t port) {
	shutdown();
	shuttingdown.store(false, std::memory_order_release);
	if (!voice.initialize()) {
		setConnectionError(voice.getLastError());
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(statemutex);
		connectionerror.clear();
		connectiondeadlinemilliseconds = steadyMilliseconds() + 15000;
		connected = false;
	}
	authorized.store(false, std::memory_order_release);
	readyqueued.store(false, std::memory_order_release);

	client = std::make_unique<znet::Client>(znet::ClientConfig{
			serverip, port, std::chrono::seconds(10), znet::ConnectionType::ZDT});
	client->SetEventCallback([this](znet::Event& event) {
		znet::EventDispatcher dispatcher{event};
		dispatcher.Dispatch<znet::ClientConnectedToServerEvent>(ZNET_BIND_FN(onConnected));
		dispatcher.Dispatch<znet::ClientDisconnectedFromServerEvent>(ZNET_BIND_FN(onDisconnected));
	});
	connectionthread = std::thread([this]() {
		if (client->Bind() != znet::Result::Success) {
			setConnectionError("Could not bind the ZDT client");
			return;
		}
		if (client->Connect() != znet::Result::Success) {
			setConnectionError("Could not connect to the ZDT server");
		}
	});
	return true;
}

void VoiceDemoClient::update() {
	std::shared_ptr<znet::PeerSession> currentsession;
	{
		std::lock_guard<std::mutex> lock(statemutex);
		currentsession = session;
		if (!currentsession && connectiondeadlinemilliseconds != 0 &&
				steadyMilliseconds() >= connectiondeadlinemilliseconds) {
			connectiondeadlinemilliseconds = 0;
			connectionerror = "Timed out waiting for the encrypted ZDT session";
		}
	}
	if (!currentsession) return;
	if (!readyqueued.load(std::memory_order_acquire) &&
			currentsession->SendPacket(std::make_shared<VoiceDemoReadyPacket>(), getVoiceDemoReadySendOptions())) {
		readyqueued.store(true, std::memory_order_release);
	}
	voice.updateNetwork(*currentsession);
}

void VoiceDemoClient::shutdown() {
	shuttingdown.store(true, std::memory_order_release);
	voice.stopTransmitting();
	if (connectionthread.joinable()) connectionthread.join();
	if (client) {
		client->Disconnect();
		client->Wait();
	}
	{
		std::lock_guard<std::mutex> lock(statemutex);
		session.reset();
		connected = false;
		connectiondeadlinemilliseconds = 0;
		connectionerror.clear();
	}
	authorized.store(false, std::memory_order_release);
	readyqueued.store(false, std::memory_order_release);
	voice.resetSession();
	voice.shutdown();
	client.reset();
}

void VoiceDemoClient::startTransmitting() {
	voice.startTransmitting();
}

void VoiceDemoClient::stopTransmitting() {
	voice.stopTransmitting();
}

VoiceDemoClient::Status VoiceDemoClient::getStatus() const {
	Status status;
	status.initialized = voice.isInitialized();
	status.authorized = authorized.load(std::memory_order_acquire);
	status.transmitting = voice.isTransmitting();
	status.stats = voice.getStats();
	status.error = voice.getLastError();
	{
		std::lock_guard<std::mutex> lock(statemutex);
		status.connected = connected;
		if (status.error.empty()) status.error = connectionerror;
	}
	return status;
}

bool VoiceDemoClient::onConnected(znet::ClientConnectedToServerEvent& event) {
	auto currentsession = event.session();
	auto codec = std::make_shared<znet::Codec>();
	registerVoiceDemoReadyPacket(*codec);
	gRegisterTeamVoicePackets(*codec, [this](gTeamVoicePacketError error) {
		reportMalformedPacket(error);
	});
	currentsession->SetCodec(codec);
	currentsession->SetHandler(std::make_shared<VoiceDemoClientPacketHandler>(this));
	{
		std::lock_guard<std::mutex> lock(statemutex);
		session = currentsession;
		connected = true;
		connectionerror.clear();
		connectiondeadlinemilliseconds = 0;
	}
	readyqueued.store(currentsession->SendPacket(std::make_shared<VoiceDemoReadyPacket>(),
			getVoiceDemoReadySendOptions()), std::memory_order_release);
	return false;
}

bool VoiceDemoClient::onDisconnected(znet::ClientDisconnectedFromServerEvent&) {
	{
		std::lock_guard<std::mutex> lock(statemutex);
		session.reset();
		connected = false;
		connectiondeadlinemilliseconds = 0;
		if (!shuttingdown.load(std::memory_order_acquire)) connectionerror = "Disconnected from the ZDT server";
	}
	readyqueued.store(false, std::memory_order_release);
	authorized.store(false, std::memory_order_release);
	voice.stopTransmitting();
	voice.resetSession();
	return false;
}

void VoiceDemoClient::handleSessionPacket(const gTeamVoiceSessionPacket& packet) {
	voice.handleSessionPacket(packet);
	authorized.store(packet.enabled && gValidateTeamVoiceSessionPacket(packet) == gTeamVoicePacketError::NONE,
			std::memory_order_release);
}

void VoiceDemoClient::handleVoicePacket(const gTeamVoiceDownlinkPacket& packet) {
	voice.handleVoicePacket(packet);
}

void VoiceDemoClient::reportMalformedPacket(gTeamVoicePacketError error) {
	voice.reportMalformedPacket(error);
}

void VoiceDemoClient::setConnectionError(const std::string& error) {
	std::lock_guard<std::mutex> lock(statemutex);
	connectionerror = error;
	connectiondeadlinemilliseconds = 0;
	connected = false;
}

std::int64_t VoiceDemoClient::steadyMilliseconds() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
}
