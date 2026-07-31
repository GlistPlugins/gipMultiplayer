#include "GameBackendLocal.h"

#include <algorithm>
#include <chrono>
#include <random>


namespace {

std::uint64_t makeVoiceSessionId() {
	std::mt19937_64 generator(static_cast<std::uint64_t>(
			std::chrono::high_resolution_clock::now().time_since_epoch().count()));
	std::uint64_t value = generator();
	return value == 0 ? 1 : value;
}

std::int64_t steadyMilliseconds() {
	return std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::shared_ptr<znet::Codec> makeServerCodec(gTeamVoiceServer* router) {
	auto codec = std::make_shared<znet::Codec>();
	codec->Add(PACKET_NODE_STATE, std::make_unique<NodeStateSerializer>());
	codec->Add(PACKET_NODE_LEAVE, std::make_unique<NodeLeaveSerializer>());
	codec->Add(PACKET_CLIENT_READY, std::make_unique<ClientReadySerializer>());
	gRegisterTeamVoicePackets(*codec, [router](gTeamVoicePacketError error) {
		router->reportMalformedPacket(error);
	});
	return codec;
}

std::shared_ptr<znet::Codec> makeLocalVoiceCodec(GameBackendLocal* backend) {
	auto codec = std::make_shared<znet::Codec>();
	codec->Add(PACKET_NODE_STATE, std::make_unique<NodeStateSerializer>());
	codec->Add(PACKET_NODE_LEAVE, std::make_unique<NodeLeaveSerializer>());
	codec->Add(PACKET_CLIENT_READY, std::make_unique<ClientReadySerializer>());
	gRegisterTeamVoicePackets(*codec, [backend](gTeamVoicePacketError error) {
		backend->reportMalformedVoicePacket(error);
	});
	return codec;
}

}

class ServerPacketHandler : public znet::PacketHandler<ServerPacketHandler, NodeStatePacket, NodeLeavePacket,
		ClientReadyPacket, gTeamVoiceUplinkPacket> {
public:
	ServerPacketHandler(GameBackendLocal* backend, const std::shared_ptr<znet::PeerSession>& session)
			: backend(backend), connectionid(session->id()), peersession(session) {
	}

	void OnPacket(std::shared_ptr<NodeStatePacket> packet) {
		auto session = peersession.lock();
		if (!session) return;
		backend->enqueueState(packet->netid, packet->x, packet->y, packet->z);
		session->SetUserPointer(std::make_shared<uint32_t>(packet->netid));
		backend->broadcast(packet, session.get());
	}

	void OnPacket(std::shared_ptr<NodeLeavePacket>) {
	}

	void OnPacket(std::shared_ptr<ClientReadyPacket>) {
		backend->authorizeVoicePeer(connectionid);
	}

	void OnPacket(std::shared_ptr<gTeamVoiceUplinkPacket> packet) {
		backend->handleVoiceUplink(connectionid, *packet);
	}

	void OnUnknown(std::shared_ptr<znet::Packet>) {
	}

private:
	GameBackendLocal* backend;
	gTeamVoiceServer::ConnectionId connectionid;
	std::weak_ptr<znet::PeerSession> peersession;
};

class LocalVoicePacketHandler : public znet::PacketHandler<LocalVoicePacketHandler, NodeStatePacket, NodeLeavePacket,
		gTeamVoiceSessionPacket, gTeamVoiceDownlinkPacket> {
public:
	explicit LocalVoicePacketHandler(GameBackendLocal* backend) : backend(backend) {
	}

	void OnPacket(std::shared_ptr<NodeStatePacket>) {
	}

	void OnPacket(std::shared_ptr<NodeLeavePacket>) {
	}

	void OnPacket(std::shared_ptr<gTeamVoiceSessionPacket> packet) {
		backend->handleVoiceSessionPacket(*packet);
	}

	void OnPacket(std::shared_ptr<gTeamVoiceDownlinkPacket> packet) {
		backend->handleVoiceDownlinkPacket(*packet);
	}

	void OnUnknown(std::shared_ptr<znet::Packet>) {
	}

private:
	GameBackendLocal* backend;
};

GameBackendLocal::GameBackendLocal(const std::string& bindIp, uint16_t port)
		: bindip(bindIp), port(port), voicesessionid(makeVoiceSessionId()) {
}

GameBackendLocal::~GameBackendLocal() {
	stopVoiceTransmission();
	if (localvoiceconnectionthread.joinable()) localvoiceconnectionthread.join();
	if (localvoiceclient) {
		localvoiceclient->Disconnect();
		localvoiceclient->Wait();
	}
	{
		std::lock_guard<std::mutex> lock(localvoicesessionmutex);
		localvoicesession.reset();
	}
	if (server) {
		server->Stop();
		server->Wait();
	}
	{
		std::lock_guard<std::mutex> lock(sessionsmutex);
		sessions.clear();
	}
	voicerouter.reset();
	shutdownVoice();
}

void GameBackendLocal::start() {
	initializeVoice();
	server = std::make_unique<znet::Server>(znet::ServerConfig{
			bindip, port, std::chrono::seconds(10), znet::ConnectionType::ZDT});
	server->SetEventCallback([this](znet::Event& event) {
		znet::EventDispatcher dispatcher{event};
		dispatcher.Dispatch<znet::ServerClientConnectedEvent>(ZNET_BIND_FN(onPeerConnected));
		dispatcher.Dispatch<znet::ServerClientDisconnectedEvent>(ZNET_BIND_FN(onPeerDisconnected));
	});
	if (server->Bind() != znet::Result::Success || server->Listen() != znet::Result::Success) {
		setVoiceTransportState(false, "Could not start the ZDT server");
		return;
	}

	std::string localaddress = bindip == "0.0.0.0" ? "127.0.0.1" : bindip;
	localvoiceclient = std::make_unique<znet::Client>(znet::ClientConfig{
			localaddress, port, std::chrono::seconds(10), znet::ConnectionType::ZDT});
	localvoiceclient->SetEventCallback([this](znet::Event& event) {
		znet::EventDispatcher dispatcher{event};
		dispatcher.Dispatch<znet::ClientConnectedToServerEvent>(ZNET_BIND_FN(onLocalVoiceConnected));
		dispatcher.Dispatch<znet::ClientDisconnectedFromServerEvent>(ZNET_BIND_FN(onLocalVoiceDisconnected));
	});
	localconnectiondeadlinemilliseconds.store(steadyMilliseconds() + 15000, std::memory_order_release);
	localvoiceconnectionthread = std::thread([this]() {
		if (localvoiceclient->Bind() != znet::Result::Success) {
			std::lock_guard<std::mutex> lock(localvoicesessionmutex);
			localconnectiondeadlinemilliseconds.store(0, std::memory_order_release);
			setVoiceTransportState(false, "Could not bind the host voice ZDT client");
			return;
		}
		if (localvoiceclient->Connect() != znet::Result::Success) {
			std::lock_guard<std::mutex> lock(localvoicesessionmutex);
			localconnectiondeadlinemilliseconds.store(0, std::memory_order_release);
			setVoiceTransportState(false, "Could not connect the host voice ZDT client");
		}
	});
}

void GameBackendLocal::broadcast(const std::shared_ptr<znet::Packet>& packet, znet::PeerSession* exclude) {
	std::lock_guard<std::mutex> lock(sessionsmutex);
	for (auto& session : sessions) {
		if (session && session.get() != exclude) session->SendPacket(packet);
	}
}

void GameBackendLocal::handleVoiceUplink(gTeamVoiceServer::ConnectionId connectionid,
		const gTeamVoiceUplinkPacket& packet) {
	voicerouter.handleVoicePacket(connectionid, packet);
}

void GameBackendLocal::authorizeVoicePeer(gTeamVoiceServer::ConnectionId connectionid) {
	// This demo has no login system, so the server deliberately assigns every
	// ready connection to its single team. Production code must source these
	// values from authenticated game state.
	gTeamVoiceServer::PeerState state;
	state.playerid = connectionid;
	state.teamid = 1;
	state.sessionid = voicesessionid;
	state.cantransmit = true;
	state.canreceive = true;
	bool published = voicerouter.setPeerState(connectionid, state);
	std::lock_guard<std::mutex> lock(pendingvoicemutex);
	if (published) {
		pendingvoicepeers.erase(connectionid);
	} else if (connectedvoicepeers.find(connectionid) != connectedvoicepeers.end()) {
		pendingvoicepeers.insert(connectionid);
	}
}

void GameBackendLocal::retryVoiceAuthorizations() {
	auto now = std::chrono::steady_clock::now();
	std::vector<gTeamVoiceServer::ConnectionId> pending;
	{
		std::lock_guard<std::mutex> lock(pendingvoicemutex);
		if (now < nextvoiceretry) return;
		nextvoiceretry = now + std::chrono::milliseconds(250);
		pending.assign(pendingvoicepeers.begin(), pendingvoicepeers.end());
	}
	for (auto connectionid : pending) authorizeVoicePeer(connectionid);
}

bool GameBackendLocal::onPeerConnected(znet::ServerClientConnectedEvent& event) {
	auto session = event.session();
	session->SetCodec(makeServerCodec(&voicerouter));
	if (!voicerouter.addPeer(session)) {
		session->Close();
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(pendingvoicemutex);
		connectedvoicepeers.insert(session->id());
	}
	session->SetHandler(std::make_shared<ServerPacketHandler>(this, session));
	std::lock_guard<std::mutex> lock(sessionsmutex);
	sessions.push_back(session);
	return false;
}

bool GameBackendLocal::onPeerDisconnected(znet::ServerClientDisconnectedEvent& event) {
	voicerouter.removePeer(event.session()->id());
	{
		std::lock_guard<std::mutex> lock(pendingvoicemutex);
		connectedvoicepeers.erase(event.session()->id());
		pendingvoicepeers.erase(event.session()->id());
	}
	uint32_t leavingid = 0;
	auto idptr = event.session()->user_ptr_typed<uint32_t>();
	if (idptr) leavingid = *idptr;
	{
		std::lock_guard<std::mutex> lock(sessionsmutex);
		sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
				[&](const std::shared_ptr<znet::PeerSession>& session) {
					return session.get() == event.session().get();
				}), sessions.end());
	}
	if (leavingid != 0) {
		enqueueLeave(leavingid);
		auto packet = std::make_shared<NodeLeavePacket>();
		packet->netid = leavingid;
		broadcast(packet);
	}
	return false;
}

bool GameBackendLocal::onLocalVoiceConnected(znet::ClientConnectedToServerEvent& event) {
	auto session = event.session();
	session->SetCodec(makeLocalVoiceCodec(this));
	session->SetHandler(std::make_shared<LocalVoicePacketHandler>(this));
	{
		std::lock_guard<std::mutex> lock(localvoicesessionmutex);
		localvoicesession = session;
		localconnectiondeadlinemilliseconds.store(0, std::memory_order_release);
		setVoiceTransportState(true);
	}
	localreadyqueued.store(session->SendPacket(std::make_shared<ClientReadyPacket>(), getGameControlSendOptions()),
			std::memory_order_release);
	return false;
}

bool GameBackendLocal::onLocalVoiceDisconnected(znet::ClientDisconnectedFromServerEvent&) {
	{
		std::lock_guard<std::mutex> lock(localvoicesessionmutex);
		localvoicesession.reset();
		localconnectiondeadlinemilliseconds.store(0, std::memory_order_release);
		setVoiceTransportState(false, "Host voice ZDT client disconnected");
	}
	localreadyqueued.store(false, std::memory_order_release);
	resetVoiceSession();
	return false;
}

std::shared_ptr<znet::PeerSession> GameBackendLocal::getVoiceSessionSnapshot() {
	retryVoiceAuthorizations();
	std::shared_ptr<znet::PeerSession> snapshot;
	{
		std::lock_guard<std::mutex> lock(localvoicesessionmutex);
		snapshot = localvoicesession;
		std::int64_t deadline = localconnectiondeadlinemilliseconds.load(std::memory_order_acquire);
		if (!snapshot && deadline != 0 && steadyMilliseconds() >= deadline &&
				localconnectiondeadlinemilliseconds.exchange(0, std::memory_order_acq_rel) != 0) {
			setVoiceTransportState(false, "Timed out waiting for the host encrypted ZDT session");
		}
	}
	if (snapshot && !localreadyqueued.load(std::memory_order_acquire) &&
			snapshot->SendPacket(std::make_shared<ClientReadyPacket>(), getGameControlSendOptions())) {
		localreadyqueued.store(true, std::memory_order_release);
	}
	return snapshot;
}

void GameBackendLocal::broadcastState(uint32_t netid, float x, float y, float z) {
	auto packet = std::make_shared<NodeStatePacket>();
	packet->netid = netid;
	packet->x = x;
	packet->y = y;
	packet->z = z;
	broadcast(packet);
}
