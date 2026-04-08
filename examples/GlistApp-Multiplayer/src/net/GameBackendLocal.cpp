#include "GameBackendLocal.h"
#include <algorithm>

// Handles packets arriving from a connected client.
// When a client sends its position (NodeStatePacket):
//   1. Enqueues it into the host's backend so the host sees the remote player.
//   2. Tags the session with the sender's ID (for identification on disconnect).
//   3. Rebroadcasts the packet to all OTHER connected clients.
class ServerPacketHandler : public znet::PacketHandler<ServerPacketHandler, NodeStatePacket, NodeLeavePacket> {
public:
	ServerPacketHandler(GameBackendLocal* b, std::shared_ptr<znet::PeerSession> s) : backend(b), peersession(std::move(s)) {}

	void OnPacket(std::shared_ptr<NodeStatePacket> p) {
		// Let the host's game loop know about this remote node
		backend->enqueueState(p->netid, p->x, p->y, p->z);
		// Store the node ID on the session so we can retrieve it on disconnect.
		// znet's user pointer feature lets us attach arbitrary data to a session.
		peersession->SetUserPointer(std::make_shared<uint32_t>(p->netid));
		// Forward to every other client (not back to the sender)
		backend->broadcast(p, peersession.get());
	}
	void OnUnknown(std::shared_ptr<znet::Packet>) {}

private:
	GameBackendLocal* backend;
	std::shared_ptr<znet::PeerSession> peersession;
};

// Creates a Codec that knows how to serialize/deserialize our packet types.
// Each session gets its own Codec instance so the library knows how to
// encode outgoing packets and decode incoming ones.
static std::shared_ptr<znet::Codec> makeCodec() {
	auto codec = std::make_shared<znet::Codec>();
	codec->Add(PACKET_NODE_STATE, std::make_unique<NodeStateSerializer>());
	codec->Add(PACKET_NODE_LEAVE, std::make_unique<NodeLeaveSerializer>());
	return codec;
}

GameBackendLocal::GameBackendLocal(const std::string& bindIp, uint16_t port)
	: bindip(bindIp), port(port) {
}

void GameBackendLocal::start() {
	server = std::make_unique<znet::Server>(znet::ServerConfig{bindip, port});

	// znet fires events on a background network thread.
	// We dispatch them to our member functions using ZNET_BIND_FN.
	server->SetEventCallback([this](znet::Event& ev) {
		znet::EventDispatcher d{ev};
		d.Dispatch<znet::ServerClientConnectedEvent>(ZNET_BIND_FN(onPeerConnected));
		d.Dispatch<znet::ServerClientDisconnectedEvent>(ZNET_BIND_FN(onPeerDisconnected));
	});

	server->Bind();
	server->Listen(); // Non-blocking, starts accepting connections in the background
}

// Sends a packet to all connected clients, optionally excluding one (the sender).
void GameBackendLocal::broadcast(const std::shared_ptr<znet::Packet>& packet, znet::PeerSession* exclude) {
	std::lock_guard<std::mutex> lk(sessionsmutex);
	for (auto& s : sessions) {
		if (s && s.get() != exclude) s->SendPacket(packet);
	}
}

// Called on the network thread when a new client connects.
// Sets up the session with our codec and packet handler, then adds it to the list.
bool GameBackendLocal::onPeerConnected(znet::ServerClientConnectedEvent& e) {
	auto sess = e.session();
	sess->SetCodec(makeCodec());
	sess->SetHandler(std::make_shared<ServerPacketHandler>(this, sess));
	std::lock_guard<std::mutex> lk(sessionsmutex);
	sessions.push_back(sess);
	return false;
}

// Called on the network thread when a client disconnects.
// Retrieves the node ID we stored on the session, removes the session,
// and notifies both the host backend and remaining clients.
bool GameBackendLocal::onPeerDisconnected(znet::ServerClientDisconnectedEvent& e) {
	// Retrieve the node ID we tagged this session with in ServerPacketHandler
	uint32_t leavingid = 0;
	auto idptr = e.session()->user_ptr_typed<uint32_t>();
	if (idptr) leavingid = *idptr;

	// Remove the disconnected session from our list
	{
		std::lock_guard<std::mutex> lk(sessionsmutex);
		sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
			[&](const std::shared_ptr<znet::PeerSession>& s) { return s.get() == e.session().get(); }),
			sessions.end());
	}

	if (leavingid != 0) {
		// Queue a leave event for the host's game loop
		enqueueLeave(leavingid);
		// Tell all remaining clients to remove this node
		auto lp = std::make_shared<NodeLeavePacket>();
		lp->netid = leavingid;
		broadcast(lp);
	}
	return false;
}

// Called by GameBackend::update() for each local node every frame.
// Sends the node's current position to all connected clients.
void GameBackendLocal::broadcastState(uint32_t netid, float x, float y, float z) {
	auto p = std::make_shared<NodeStatePacket>();
	p->netid = netid;
	p->x = x;
	p->y = y;
	p->z = z;
	broadcast(p);
}
