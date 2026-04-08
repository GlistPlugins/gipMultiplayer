#include "GameBackendRemote.h"

// Handles packets arriving from the server.
// The server broadcasts other clients' positions to us as NodeStatePackets,
// and notifies us when a client leaves via NodeLeavePacket.
// Both are enqueued for the main thread to process in GameBackend::update().
class ClientPacketHandler : public znet::PacketHandler<ClientPacketHandler, NodeStatePacket, NodeLeavePacket> {
public:
	ClientPacketHandler(GameBackendRemote* b) : backend(b) {}

	void OnPacket(std::shared_ptr<NodeStatePacket> p) { backend->enqueueState(p->netid, p->x, p->y, p->z); }
	void OnPacket(std::shared_ptr<NodeLeavePacket> p) { backend->enqueueLeave(p->netid); }
	void OnUnknown(std::shared_ptr<znet::Packet>) {}

private:
	GameBackendRemote* backend;
};

static std::shared_ptr<znet::Codec> makeCodec() {
	auto codec = std::make_shared<znet::Codec>();
	codec->Add(PACKET_NODE_STATE, std::make_unique<NodeStateSerializer>());
	codec->Add(PACKET_NODE_LEAVE, std::make_unique<NodeLeaveSerializer>());
	return codec;
}

GameBackendRemote::GameBackendRemote(const std::string& serverIp, uint16_t port)
	: serverip(serverIp), port(port) {
}

void GameBackendRemote::start() {
	client = std::make_unique<znet::Client>(znet::ClientConfig{serverip, port});

	// znet fires events on a background network thread.
	// We dispatch them to our member functions using ZNET_BIND_FN.
	client->SetEventCallback([this](znet::Event& ev) {
		znet::EventDispatcher d{ev};
		d.Dispatch<znet::ClientConnectedToServerEvent>(ZNET_BIND_FN(onConnected));
		d.Dispatch<znet::ClientDisconnectedFromServerEvent>(ZNET_BIND_FN(onDisconnected));
	});

	client->Bind();
	client->Connect(); // Non-blocking, connects in the background
}

// Called on the network thread when we successfully connect to the server.
// Sets up the session with our codec and packet handler so we can
// start sending and receiving packets.
bool GameBackendRemote::onConnected(znet::ClientConnectedToServerEvent& e) {
	auto sess = e.session();
	sess->SetCodec(makeCodec());
	sess->SetHandler(std::make_shared<ClientPacketHandler>(this));
	session = sess;
	return false;
}

// Called on the network thread when we lose connection to the server.
bool GameBackendRemote::onDisconnected(znet::ClientDisconnectedFromServerEvent& e) {
	session.reset();
	return false;
}

// Called by GameBackend::update() for each local node every frame.
// Sends the node's current position to the server, which then
// rebroadcasts it to all other connected clients.
void GameBackendRemote::broadcastState(uint32_t netid, float x, float y, float z) {
	if (!session) return;
	auto p = std::make_shared<NodeStatePacket>();
	p->netid = netid;
	p->x = x;
	p->y = y;
	p->z = z;
	session->SendPacket(p);
}
