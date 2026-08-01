#include "voice/VoiceDemoServer.h"

#include "voice/VoiceDemoPackets.h"

#include "znet/event.h"
#include "znet/packet_handler.h"

#include <chrono>
#include <random>
#include <vector>


class VoiceDemoServerPacketHandler : public znet::PacketHandler<VoiceDemoServerPacketHandler,
		VoiceDemoReadyPacket, gTeamVoiceUplinkPacket> {
public:
	VoiceDemoServerPacketHandler(VoiceDemoServer* owner, gTeamVoiceServer::ConnectionId connectionid)
			: owner(owner), connectionid(connectionid) {
	}

	void OnPacket(std::shared_ptr<VoiceDemoReadyPacket>) {
		owner->authorizePeer(connectionid);
	}

	void OnPacket(std::shared_ptr<gTeamVoiceUplinkPacket> packet) {
		owner->handleVoicePacket(connectionid, *packet);
	}

	void OnUnknown(std::shared_ptr<znet::Packet>) {
	}

private:
	VoiceDemoServer* owner;
	gTeamVoiceServer::ConnectionId connectionid;
};

VoiceDemoServer::VoiceDemoServer() {
}

VoiceDemoServer::~VoiceDemoServer() {
	shutdown();
}

bool VoiceDemoServer::start(const std::string& bindip, std::uint16_t port) {
	shutdown();
	sessionid = makeSessionId();
	server = std::make_unique<znet::Server>(znet::ServerConfig{
			bindip, port, std::chrono::seconds(10), znet::ConnectionType::ZDT});
	server->SetEventCallback([this](znet::Event& event) {
		znet::EventDispatcher dispatcher{event};
		dispatcher.Dispatch<znet::ServerClientConnectedEvent>(ZNET_BIND_FN(onPeerConnected));
		dispatcher.Dispatch<znet::ServerClientDisconnectedEvent>(ZNET_BIND_FN(onPeerDisconnected));
	});
	if (server->Bind() != znet::Result::Success || server->Listen() != znet::Result::Success) {
		std::lock_guard<std::mutex> lock(statemutex);
		error = "Could not start the ZDT voice server";
		return false;
	}
	std::lock_guard<std::mutex> lock(statemutex);
	running = true;
	error.clear();
	return true;
}

void VoiceDemoServer::update() {
	auto now = std::chrono::steady_clock::now();
	std::vector<gTeamVoiceServer::ConnectionId> pending;
	{
		std::lock_guard<std::mutex> lock(statemutex);
		if (!running || now < nextretry) return;
		nextretry = now + std::chrono::milliseconds(250);
		pending.assign(pendingpeers.begin(), pendingpeers.end());
	}
	for (auto connectionid : pending) authorizePeer(connectionid);
}

void VoiceDemoServer::shutdown() {
	if (server) {
		server->Stop();
		server->Wait();
	}
	server.reset();
	router.reset();
	std::lock_guard<std::mutex> lock(statemutex);
	connectedpeers.clear();
	pendingpeers.clear();
	error.clear();
	running = false;
}

VoiceDemoServer::Status VoiceDemoServer::getStatus() const {
	Status status;
	status.stats = router.getStats();
	std::lock_guard<std::mutex> lock(statemutex);
	status.running = running;
	status.error = error;
	return status;
}

bool VoiceDemoServer::onPeerConnected(znet::ServerClientConnectedEvent& event) {
	auto peersession = event.session();
	auto codec = std::make_shared<znet::Codec>();
	registerVoiceDemoReadyPacket(*codec);
	gRegisterTeamVoicePackets(*codec, [this](gTeamVoicePacketError error) {
		router.reportMalformedPacket(error);
	});
	peersession->SetCodec(codec);
	if (!router.addPeer(peersession)) {
		peersession->Close();
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(statemutex);
		connectedpeers.insert(peersession->id());
	}
	peersession->SetHandler(std::make_shared<VoiceDemoServerPacketHandler>(this, peersession->id()));
	return false;
}

bool VoiceDemoServer::onPeerDisconnected(znet::ServerClientDisconnectedEvent& event) {
	router.removePeer(event.session()->id());
	std::lock_guard<std::mutex> lock(statemutex);
	connectedpeers.erase(event.session()->id());
	pendingpeers.erase(event.session()->id());
	return false;
}

void VoiceDemoServer::authorizePeer(gTeamVoiceServer::ConnectionId connectionid) {
	gTeamVoiceServer::PeerState state;
	state.playerid = connectionid;
	state.teamid = 1;
	state.sessionid = sessionid;
	state.cantransmit = true;
	state.canreceive = true;
	bool published = router.setPeerState(connectionid, state);
	std::lock_guard<std::mutex> lock(statemutex);
	if (published) {
		pendingpeers.erase(connectionid);
	} else if (connectedpeers.find(connectionid) != connectedpeers.end()) {
		pendingpeers.insert(connectionid);
	}
}

void VoiceDemoServer::handleVoicePacket(gTeamVoiceServer::ConnectionId connectionid,
		const gTeamVoiceUplinkPacket& packet) {
	router.handleVoicePacket(connectionid, packet);
}

std::uint64_t VoiceDemoServer::makeSessionId() {
	std::mt19937_64 generator(static_cast<std::uint64_t>(
			std::chrono::high_resolution_clock::now().time_since_epoch().count()));
	std::uint64_t value = generator();
	return value == 0 ? 1 : value;
}
