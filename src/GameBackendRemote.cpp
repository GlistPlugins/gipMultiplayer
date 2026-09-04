#include "GameBackendRemote.h"
#include "voice/gTeamVoicePackets.h"
#include <memory>

// Handles packets arriving from the server.
// The server broadcasts other clients' positions to us as NodeStatePackets,
// and notifies us when a client leaves via NodeLeavePacket.
// Both are enqueued for the main thread to process in GameBackend::update().
class ClientPacketHandler : public znet::PacketHandler<ClientPacketHandler,
	NodeStatePacket, NodeLeavePacket, PlayerFirePacket, PlayerHitPacket, PlayerKilledPacket,
	LobbyStatePacket, StartMatchPacket, LobbyKickPacket, KeepAlivePacket, PingPacket, PongPacket,
	gTeamVoiceSessionPacket, gTeamVoiceDownlinkPacket> {
public:
	ClientPacketHandler(GameBackendRemote* b) : backend(b) {}

	void OnPacket(std::shared_ptr<NodeStatePacket> p) { backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p)); }
	void OnPacket(std::shared_ptr<NodeLeavePacket> p) { backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p)); }

	//Fire and Hit PacketHandler
	void OnPacket(std::shared_ptr<PlayerFirePacket> p) {backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));}
	void OnPacket(std::shared_ptr<PlayerHitPacket> p) {backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));}
	void OnPacket(std::shared_ptr<PlayerKilledPacket> p) {backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));}

	// Lobby callbacks
	void OnPacket(std::shared_ptr<LobbyStatePacket> p) {backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));}
	void OnPacket(std::shared_ptr<StartMatchPacket> p) {backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));}
	void OnPacket(std::shared_ptr<LobbyKickPacket> p) {backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));}
	void OnPacket(std::shared_ptr<KeepAlivePacket> p) {backend->enqueuePacket(std::static_pointer_cast<znet::Packet>(p));}

	void OnPacket(std::shared_ptr<PingPacket> p) {
		auto pong = std::make_shared<PongPacket>();
		pong->timestamp = p->timestamp;
		backend->sendPacket(pong);
	}
	void OnPacket(std::shared_ptr<PongPacket> p) {
		backend->onPongReceived(p->timestamp);
	}

	// Voice callbacks
	void OnPacket(std::shared_ptr<gTeamVoiceSessionPacket> p) {
		backend->handleVoiceSessionPacket(*p);
	}
	void OnPacket(std::shared_ptr<gTeamVoiceDownlinkPacket> p) {
		backend->handleVoiceDownlinkPacket(*p);
	}

	void OnUnknown(std::shared_ptr<znet::Packet>) {}

private:
	GameBackendRemote* backend;
};

static std::shared_ptr<znet::Codec> makeCodec() {
	auto codec = std::make_shared<znet::Codec>();
	codec->Add(PACKET_NODE_STATE, std::make_unique<NodeStateSerializer>());
	codec->Add(PACKET_NODE_LEAVE, std::make_unique<NodeLeaveSerializer>());

	codec->Add(PACKET_NODE_FIRE, std::make_unique<PlayerFireSerializer>());
	codec->Add(PACKET_NODE_HIT, std::make_unique<PlayerHitSerializer>());
	codec->Add(PACKET_NODE_KILLED, std::make_unique<PlayerKilledSerializer>());

	codec->Add(PACKET_SERVER_QUERY_REQ, std::make_unique<ServerQueryReqSerializer>());
	codec->Add(PACKET_SERVER_QUERY_RES, std::make_unique<ServerQueryResSerializer>());
	codec->Add(PACKET_LOBBY_JOIN, std::make_unique<LobbyJoinSerializer>());
	codec->Add(PACKET_LOBBY_STATE, std::make_unique<LobbyStateSerializer>());
	codec->Add(PACKET_TOGGLE_READY, std::make_unique<ToggleReadySerializer>());
	codec->Add(PACKET_SWITCH_TEAM, std::make_unique<SwitchTeamSerializer>());
	codec->Add(PACKET_START_MATCH, std::make_unique<StartMatchSerializer>());
	codec->Add(PACKET_LOBBY_KICK, std::make_unique<LobbyKickSerializer>());
	codec->Add(PACKET_KEEPALIVE, std::make_unique<KeepAliveSerializer>());
	codec->Add(PACKET_PING, std::make_unique<PingSerializer>());
	codec->Add(PACKET_PONG, std::make_unique<PongSerializer>());

	// Voice Packets
	codec->Add(G_TEAM_VOICE_SESSION_PACKET_ID, std::make_unique<gTeamVoiceSessionSerializer>());
	codec->Add(G_TEAM_VOICE_UPLINK_PACKET_ID, std::make_unique<gTeamVoiceUplinkSerializer>());
	codec->Add(G_TEAM_VOICE_DOWNLINK_PACKET_ID, std::make_unique<gTeamVoiceDownlinkSerializer>());
	
	return codec;
}

GameBackendRemote::GameBackendRemote(const std::string& serverIp, uint16_t port)
	: serverip(serverIp), port(port) {
}

GameBackendRemote::GameBackendRemote(gipP2PSession punched)
	: punchHost(std::move(punched.host)) {
	session = std::move(punched.session);
}

GameBackendRemote::~GameBackendRemote() {
	// Safely release the session before the client is destroyed,
	// because the session might depend on internal client state to cleanly shutdown.
	std::shared_ptr<znet::PeerSession> closing;
	{
		std::lock_guard<std::mutex> lk(sessionmutex);
		closing.swap(session);
		pendingPackets.clear();
	}
	// Outside the lock: Disconnect() waits on the network thread, which is
	// where onDisconnected runs and takes the same mutex.
	if (closing) closing->Close();
	if (punchHost) punchHost->Stop();
	if (client) client->Disconnect();
}

void GameBackendRemote::start() {
	initializeVoice();
	// A punched session arrives already handshaken, so it only needs wiring up.
	if (session) {
		adoptSession(session);
		return;
	}

	client = std::make_unique<znet::Client>(znet::ClientConfig{serverip, port, std::chrono::seconds(2), znet::ConnectionType::ZDT});
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

void GameBackendRemote::sendPacket(std::shared_ptr<znet::Packet> packet) {
	std::lock_guard<std::mutex> lk(sessionmutex);
	if (session && session->IsReady()) {
		if (session->SendPacket(packet) != znet::Result::Success) {
			if (packet->id() != PACKET_KEEPALIVE) {
				gLogw("GameBackendRemote") << "Failed to send packet!";
			}
		}
		return;
	}
	// A keepalive only means anything the moment it is sent, so queueing one
	// would just grow this list for as long as we stay unconnected.
	if (packet->id() == PACKET_KEEPALIVE) return;
	if (pendingPackets.size() >= MAX_PENDING_PACKETS) {
		gLogw("GameBackendRemote") << "Dropping packet, the send backlog is full";
		return;
	}
	pendingPackets.push_back(std::move(packet));
}

// Wires the session with our codec and packet handler, then flushes whatever
// was queued while there was nothing to send it on.
void GameBackendRemote::adoptSession(const std::shared_ptr<znet::PeerSession>& sess) {
	sess->SetCodec(makeCodec());
	sess->SetHandler(std::make_shared<ClientPacketHandler>(this));
	initializeVoice();

	std::vector<std::shared_ptr<znet::Packet>> queued;
	{
		std::lock_guard<std::mutex> lk(sessionmutex);
		session = sess;
		queued.swap(pendingPackets);
	}
	for (auto& p : queued) {
		sess->SendPacket(p);
	}
	notifyConnected();
}

// Called on the network thread when we successfully connect to the server.
bool GameBackendRemote::onConnected(znet::ClientConnectedToServerEvent& e) {
	adoptSession(e.session());
	return false;
}

// Called on the network thread when we lose connection to the server.
bool GameBackendRemote::onDisconnected(znet::ClientDisconnectedFromServerEvent& e) {
	{
		std::lock_guard<std::mutex> lk(sessionmutex);
		session.reset();
	}
	notifyDisconnected();
	return false;
}

// Called by GameBackend::update() for each local node every frame.
// Sends the node's current position to the server, which then
// rebroadcasts it to all other connected clients.
void GameBackendRemote::broadcastState(uint32_t netid, float x, float y, float z, float yaw, uint8_t team, uint8_t animState) {
	auto p = std::make_shared<NodeStatePacket>();
	p->netid = netid;
	p->x = x;
	p->y = y;
	p->z = z;
	p->yaw = yaw;
	p->team = team;
	p->animState = animState;
	
	std::lock_guard<std::mutex> lk(sessionmutex);
	if (session) session->SendPacket(p);
}

void GameBackendRemote::broadcastLobbyState() {
	// Clients do not broadcast lobby states.
}

void GameBackendRemote::broadcastFireEvent(uint32_t shooterId, uint8_t gunType, float ox, float oy, float oz, float dx, float dy, float dz) {
	auto p = std::make_shared<PlayerFirePacket>();
	p->shooterId = shooterId; p->gunType = gunType;
	p->originX = ox; p->originY = oy; p->originZ = oz;
	p->dirX = dx; p->dirY = dy; p->dirZ = dz;
	
	std::lock_guard<std::mutex> lk(sessionmutex);
	if(session) { session->SendPacket(p); }
}

void GameBackendRemote::broadcastHitEvent(uint32_t attackerId, uint32_t victimId, float damage) {
	auto p = std::make_shared<PlayerHitPacket>();
	p->attackerId = attackerId;
	p->victimId = victimId;
	p->damage = damage;
	std::lock_guard<std::mutex> lk(sessionmutex);
	if (session) { session->SendPacket(p); }
}

void GameBackendRemote::broadcastKillEvent(uint32_t killerId, uint32_t victimId) {
	auto p = std::make_shared<PlayerKilledPacket>();
	p->killerId = killerId;
	p->victimId = victimId;
	std::lock_guard<std::mutex> lk(sessionmutex);
	if (session) { session->SendPacket(p); }
}

void GameBackendRemote::update(float deltaTime) {
	GameBackend::update(deltaTime);

	std::shared_ptr<znet::PeerSession> currentSession;
	{
		std::lock_guard<std::mutex> lk(sessionmutex);
		currentSession = session;
	}
	if (currentSession && currentSession->IsAlive()) {
		voiceClient.updateNetwork(*currentSession);
	}
}

bool GameBackendRemote::initializeVoice() {
	return voiceClient.initialize();
}

void GameBackendRemote::shutdownVoice() {
	voiceClient.shutdown();
}

void GameBackendRemote::startVoiceTransmission() {
	if (!voiceClient.isInitialized()) {
		initializeVoice();
	}
	voiceClient.startTransmitting();
}

void GameBackendRemote::stopVoiceTransmission() {
	voiceClient.stopTransmitting();
}

bool GameBackendRemote::isVoiceTransmitting() const {
	return voiceClient.isTransmitting();
}

bool GameBackendRemote::isPlayerTalking(uint32_t playerId) const {
	auto stats = voiceClient.getSpeakerStats();
	for (const auto& s : stats) {
		if (s.speakerid == playerId && s.jitterdepth > 0) {
			return true;
		}
	}
	return false;
}

void GameBackendRemote::setSpeakerMuted(uint32_t playerId, bool muted) {
	voiceClient.setSpeakerMuted(playerId, muted);
}

void GameBackendRemote::setSpeakerVolume(uint32_t playerId, float volume) {
	voiceClient.setSpeakerVolume(playerId, volume);
}

void GameBackendRemote::setVoiceEnabled(bool enabled) {
	voiceClient.setEnabled(enabled);
}

bool GameBackendRemote::isVoiceEnabled() const {
	return voiceClient.isEnabled();
}

void GameBackendRemote::setHearEnemiesVoice(bool hear) {
	// Remote clients have routing controlled by the host, but can also filter locally
}

bool GameBackendRemote::canHearEnemiesVoice() const {
	return false;
}

void GameBackendRemote::handleVoiceSessionPacket(const gTeamVoiceSessionPacket& p) {
	voiceClient.handleSessionPacket(p);
}

void GameBackendRemote::handleVoiceDownlinkPacket(const gTeamVoiceDownlinkPacket& p) {
	voiceClient.handleVoicePacket(p);
}
