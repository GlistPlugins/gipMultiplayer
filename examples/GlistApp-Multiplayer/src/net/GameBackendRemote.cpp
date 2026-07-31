#include "GameBackendRemote.h"

#include <chrono>


namespace {

std::int64_t steadyMilliseconds() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

// Handles packets arriving from the server.
// The server broadcasts other clients' positions to us as NodeStatePackets,
// and notifies us when a client leaves via NodeLeavePacket.
// Both are enqueued for the main thread to process in GameBackend::update().
class ClientPacketHandler : public znet::PacketHandler<ClientPacketHandler, NodeStatePacket, NodeLeavePacket,
		gTeamVoiceSessionPacket, gTeamVoiceDownlinkPacket> {
public:
	ClientPacketHandler(GameBackendRemote* b) : backend(b) {}

	void OnPacket(std::shared_ptr<NodeStatePacket> p) { backend->enqueueState(p->netid, p->x, p->y, p->z); }
	void OnPacket(std::shared_ptr<NodeLeavePacket> p) { backend->enqueueLeave(p->netid); }
	void OnPacket(std::shared_ptr<gTeamVoiceSessionPacket> p) { backend->handleVoiceSessionPacket(*p); }
	void OnPacket(std::shared_ptr<gTeamVoiceDownlinkPacket> p) { backend->handleVoiceDownlinkPacket(*p); }
	void OnUnknown(std::shared_ptr<znet::Packet>) {}

private:
	GameBackendRemote* backend;
};

static std::shared_ptr<znet::Codec> makeCodec(GameBackendRemote* backend) {
	auto codec = std::make_shared<znet::Codec>();
	codec->Add(PACKET_NODE_STATE, std::make_unique<NodeStateSerializer>());
	codec->Add(PACKET_NODE_LEAVE, std::make_unique<NodeLeaveSerializer>());
	codec->Add(PACKET_CLIENT_READY, std::make_unique<ClientReadySerializer>());
	gRegisterTeamVoicePackets(*codec, [backend](gTeamVoicePacketError error) {
		backend->reportMalformedVoicePacket(error);
	});
	return codec;
}

GameBackendRemote::GameBackendRemote(const std::string& serverIp, uint16_t port)
	: serverip(serverIp), port(port) {
}

GameBackendRemote::~GameBackendRemote() {
	stopVoiceTransmission();
	if (connectionthread.joinable()) connectionthread.join();
	if (client) {
		client->Disconnect();
		client->Wait();
	}
	{
		std::lock_guard<std::mutex> lock(sessionmutex);
		session.reset();
	}
	shutdownVoice();
}

void GameBackendRemote::start() {
	initializeVoice();
	client = std::make_unique<znet::Client>(znet::ClientConfig{
			serverip, port, std::chrono::seconds(10), znet::ConnectionType::ZDT});

	// znet fires events on a background network thread.
	// We dispatch them to our member functions using ZNET_BIND_FN.
	client->SetEventCallback([this](znet::Event& ev) {
		znet::EventDispatcher d{ev};
		d.Dispatch<znet::ClientConnectedToServerEvent>(ZNET_BIND_FN(onConnected));
		d.Dispatch<znet::ClientDisconnectedFromServerEvent>(ZNET_BIND_FN(onDisconnected));
	});

	connectiondeadlinemilliseconds.store(steadyMilliseconds() + 15000, std::memory_order_release);
	connectionthread = std::thread([this]() {
		if (client->Bind() != znet::Result::Success) {
			std::lock_guard<std::mutex> lock(sessionmutex);
			connectiondeadlinemilliseconds.store(0, std::memory_order_release);
			setVoiceTransportState(false, "Could not bind the ZDT client");
			return;
		}
		if (client->Connect() != znet::Result::Success) {
			std::lock_guard<std::mutex> lock(sessionmutex);
			connectiondeadlinemilliseconds.store(0, std::memory_order_release);
			setVoiceTransportState(false, "Could not connect to the ZDT server");
		}
	});
}

// Called on the network thread when we successfully connect to the server.
// Sets up the session with our codec and packet handler so we can
// start sending and receiving packets.
bool GameBackendRemote::onConnected(znet::ClientConnectedToServerEvent& e) {
	auto sess = e.session();
	sess->SetCodec(makeCodec(this));
	sess->SetHandler(std::make_shared<ClientPacketHandler>(this));
	{
		std::lock_guard<std::mutex> lock(sessionmutex);
		session = sess;
		connectiondeadlinemilliseconds.store(0, std::memory_order_release);
		setVoiceTransportState(true);
	}
	readyqueued.store(sess->SendPacket(std::make_shared<ClientReadyPacket>(), getGameControlSendOptions()),
			std::memory_order_release);
	return false;
}

// Called on the network thread when we lose connection to the server.
bool GameBackendRemote::onDisconnected(znet::ClientDisconnectedFromServerEvent& e) {
	{
		std::lock_guard<std::mutex> lock(sessionmutex);
		session.reset();
		connectiondeadlinemilliseconds.store(0, std::memory_order_release);
		setVoiceTransportState(false, "Disconnected from the ZDT server");
	}
	readyqueued.store(false, std::memory_order_release);
	resetVoiceSession();
	return false;
}

std::shared_ptr<znet::PeerSession> GameBackendRemote::getVoiceSessionSnapshot() {
	std::shared_ptr<znet::PeerSession> snapshot;
	{
		std::lock_guard<std::mutex> lock(sessionmutex);
		snapshot = session;
		std::int64_t deadline = connectiondeadlinemilliseconds.load(std::memory_order_acquire);
		if (!snapshot && deadline != 0 && steadyMilliseconds() >= deadline &&
				connectiondeadlinemilliseconds.exchange(0, std::memory_order_acq_rel) != 0) {
			setVoiceTransportState(false, "Timed out waiting for the encrypted ZDT session");
		}
	}
	if (snapshot && !readyqueued.load(std::memory_order_acquire) &&
			snapshot->SendPacket(std::make_shared<ClientReadyPacket>(), getGameControlSendOptions())) {
		readyqueued.store(true, std::memory_order_release);
	}
	return snapshot;
}

// Called by GameBackend::update() for each local node every frame.
// Sends the node's current position to the server, which then
// rebroadcasts it to all other connected clients.
void GameBackendRemote::broadcastState(uint32_t netid, float x, float y, float z) {
	auto currentsession = getVoiceSessionSnapshot();
	if (!currentsession) return;
	auto p = std::make_shared<NodeStatePacket>();
	p->netid = netid;
	p->x = x;
	p->y = y;
	p->z = z;
	currentsession->SendPacket(p);
}
