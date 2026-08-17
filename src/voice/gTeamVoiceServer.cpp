/*
 * gTeamVoiceServer.cpp
 */

#include "voice/gTeamVoiceServer.h"

#include "znet/types.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>


namespace {

bool isValidPeerState(const gTeamVoiceServer::PeerState& state) {
	return state.playerid != 0 && state.teamid != 0 && state.sessionid != 0;
}

bool peerStatesEqual(const gTeamVoiceServer::PeerState& left, const gTeamVoiceServer::PeerState& right) {
	return left.playerid == right.playerid && left.teamid == right.teamid && left.sessionid == right.sessionid &&
			left.cantransmit == right.cantransmit && left.canreceive == right.canreceive;
}

}

class gTeamVoiceServer::State {
public:
	explicit State(const Config& config) : config(config) {
	}

	struct Peer {
		std::recursive_mutex controlmutex;
		SendCallback send;
		PeerState state;
		bool hasstate = false;
		bool controlpublished = true;
		std::uint32_t generation = 0;
		double packettokens = 0.0;
		double bytetokens = 0.0;
		std::chrono::steady_clock::time_point ratetime = std::chrono::steady_clock::now();
	};

	struct Recipient {
		SendCallback send;
		std::uint32_t generation;
	};

	static std::uint32_t nextGeneration(std::uint32_t current) {
		current++;
		if (current == 0) current++;
		return current;
	}

	bool consumeRate(Peer& peer, std::size_t bytes) {
		auto now = std::chrono::steady_clock::now();
		double elapsed = std::chrono::duration<double>(now - peer.ratetime).count();
		peer.ratetime = now;
		peer.packettokens = std::min(config.packetburst, peer.packettokens + elapsed * config.packetspersecond);
		peer.bytetokens = std::min(config.byteburst, peer.bytetokens + elapsed * config.bytespersecond);
		if (peer.packettokens < 1.0 || peer.bytetokens < static_cast<double>(bytes)) return false;
		peer.packettokens -= 1.0;
		peer.bytetokens -= static_cast<double>(bytes);
		return true;
	}

	Config config;
	mutable std::mutex mutex;
	std::unordered_map<ConnectionId, std::shared_ptr<Peer>> peers;
	std::atomic<std::uint64_t> receivedpackets{0};
	std::atomic<std::uint64_t> receivedbytes{0};
	std::atomic<std::uint64_t> relayedpackets{0};
	std::atomic<std::uint64_t> relayedbytes{0};
	std::atomic<std::uint64_t> routingrejections{0};
	std::atomic<std::uint64_t> malformedpackets{0};
	std::atomic<std::uint64_t> ratelimitedpackets{0};
	std::atomic<std::uint64_t> sendfailures{0};
};

gTeamVoiceServer::gTeamVoiceServer() : state(std::make_unique<State>(Config{})) {
}

gTeamVoiceServer::gTeamVoiceServer(const Config& config) : state(std::make_unique<State>(config)) {
}

gTeamVoiceServer::~gTeamVoiceServer() = default;

bool gTeamVoiceServer::addPeer(ConnectionId connectionid, SendCallback sendcallback) {
	if (connectionid == 0 || !sendcallback) return false;
	std::lock_guard<std::mutex> lock(state->mutex);
	if (state->peers.find(connectionid) != state->peers.end()) return false;
	if (state->peers.size() >= state->config.maxconnections) return false;
	auto peer = std::make_shared<State::Peer>();
	peer->send = std::move(sendcallback);
	peer->packettokens = state->config.packetburst;
	peer->bytetokens = state->config.byteburst;
	state->peers.emplace(connectionid, std::move(peer));
	return true;
}

bool gTeamVoiceServer::addPeer(const std::shared_ptr<znet::PeerSession>& session) {
	if (!session || !session->IsAlive() || session->connection_type() != znet::ConnectionType::ZDT) return false;
	std::weak_ptr<znet::PeerSession> weak = session;
	return addPeer(session->id(), [weak](const std::shared_ptr<znet::Packet>& packet, const znet::SendOptions& options) {
		auto locked = weak.lock();
		return locked && locked->IsAlive() && locked->SendPacket(packet, options) == znet::Result::Success;
	});
}

void gTeamVoiceServer::removePeer(ConnectionId connectionid) {
	std::lock_guard<std::mutex> lock(state->mutex);
	state->peers.erase(connectionid);
}

bool gTeamVoiceServer::setPeerState(ConnectionId connectionid, const PeerState& newstate) {
	if (!isValidPeerState(newstate)) return clearPeerState(connectionid);
	std::shared_ptr<State::Peer> peer;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		auto it = state->peers.find(connectionid);
		if (it == state->peers.end()) return false;
		peer = it->second;
	}
	std::lock_guard<std::recursive_mutex> controllock(peer->controlmutex);
	SendCallback send;
	std::uint32_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		auto it = state->peers.find(connectionid);
		if (it == state->peers.end() || it->second != peer) return false;
		if (peer->hasstate && peerStatesEqual(peer->state, newstate)) {
			if (peer->controlpublished) return true;
		} else {
			peer->state = newstate;
			peer->hasstate = true;
			peer->controlpublished = false;
			peer->generation = State::nextGeneration(peer->generation);
			peer->packettokens = state->config.packetburst;
			peer->bytetokens = state->config.byteburst;
			peer->ratetime = std::chrono::steady_clock::now();
		}
		send = peer->send;
		generation = peer->generation;
	}
	auto control = std::make_shared<gTeamVoiceSessionPacket>();
	control->enabled = true;
	control->sessionid = newstate.sessionid;
	control->membershipgeneration = generation;
	control->playerid = newstate.playerid;
	if (!send(control, gGetTeamVoiceControlSendOptions())) {
		state->sendfailures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	std::lock_guard<std::mutex> lock(state->mutex);
	auto it = state->peers.find(connectionid);
	if (it == state->peers.end() || it->second != peer || !peer->hasstate || peer->generation != generation ||
			!peerStatesEqual(peer->state, newstate)) return false;
	peer->controlpublished = true;
	return true;
}

bool gTeamVoiceServer::clearPeerState(ConnectionId connectionid) {
	std::shared_ptr<State::Peer> peer;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		auto it = state->peers.find(connectionid);
		if (it == state->peers.end()) return false;
		peer = it->second;
	}
	std::lock_guard<std::recursive_mutex> controllock(peer->controlmutex);
	SendCallback send;
	std::uint32_t generation = 0;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		auto it = state->peers.find(connectionid);
		if (it == state->peers.end() || it->second != peer) return false;
		if (!peer->hasstate && peer->controlpublished) return true;
		if (peer->hasstate) {
			peer->hasstate = false;
			peer->state = {};
			peer->generation = State::nextGeneration(peer->generation);
		}
		peer->controlpublished = false;
		send = peer->send;
		generation = peer->generation;
	}
	auto control = std::make_shared<gTeamVoiceSessionPacket>();
	control->enabled = false;
	if (!send(control, gGetTeamVoiceControlSendOptions())) {
		state->sendfailures.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	std::lock_guard<std::mutex> lock(state->mutex);
	auto it = state->peers.find(connectionid);
	if (it == state->peers.end() || it->second != peer || peer->hasstate || peer->generation != generation) return false;
	peer->controlpublished = true;
	return true;
}

void gTeamVoiceServer::handleVoicePacket(ConnectionId senderconnectionid, const gTeamVoiceUplinkPacket& packet) {
	gTeamVoicePacketError validation = gValidateTeamVoiceUplinkPacket(packet);
	if (validation != gTeamVoicePacketError::NONE) {
		reportMalformedPacket(validation);
		return;
	}
	state->receivedpackets.fetch_add(1, std::memory_order_relaxed);
	state->receivedbytes.fetch_add(packet.payload.size(), std::memory_order_relaxed);

	PeerState senderstate;
	std::uint32_t speakergeneration = 0;
	std::vector<State::Recipient> recipients;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		auto sender = state->peers.find(senderconnectionid);
		if (sender == state->peers.end() || !sender->second->hasstate || !sender->second->controlpublished ||
				!sender->second->state.cantransmit ||
				!isValidPeerState(sender->second->state) || packet.sessionid != sender->second->state.sessionid ||
				packet.membershipgeneration != sender->second->generation) {
			state->routingrejections.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (!state->consumeRate(*sender->second, packet.payload.size())) {
			state->ratelimitedpackets.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		senderstate = sender->second->state;
		speakergeneration = sender->second->generation;
		for (const auto& item : state->peers) {
			if (item.first == senderconnectionid) continue;
			const State::Peer& peer = *item.second;
			if (!peer.hasstate || !peer.controlpublished || !peer.state.canreceive || !isValidPeerState(peer.state)) continue;
			if (peer.state.sessionid != senderstate.sessionid || peer.state.teamid != senderstate.teamid) continue;
			recipients.push_back({peer.send, peer.generation});
		}
	}

	for (const State::Recipient& recipient : recipients) {
		auto relay = std::make_shared<gTeamVoiceDownlinkPacket>();
		relay->flags = packet.flags;
		relay->sessionid = senderstate.sessionid;
		relay->recipientgeneration = recipient.generation;
		relay->speakerid = senderstate.playerid;
		relay->speakergeneration = speakergeneration;
		relay->streamgeneration = packet.streamgeneration;
		relay->sequence = packet.sequence;
		relay->sampleposition = packet.sampleposition;
		relay->payload = packet.payload;
		if (recipient.send(relay, gGetTeamVoiceDataSendOptions())) {
			state->relayedpackets.fetch_add(1, std::memory_order_relaxed);
			state->relayedbytes.fetch_add(relay->payload.size(), std::memory_order_relaxed);
		} else {
			state->sendfailures.fetch_add(1, std::memory_order_relaxed);
		}
	}
}

void gTeamVoiceServer::reportMalformedPacket(gTeamVoicePacketError) {
	state->malformedpackets.fetch_add(1, std::memory_order_relaxed);
}

gTeamVoiceServer::Stats gTeamVoiceServer::getStats() const {
	return {
		state->receivedpackets.load(std::memory_order_relaxed),
		state->receivedbytes.load(std::memory_order_relaxed),
		state->relayedpackets.load(std::memory_order_relaxed),
		state->relayedbytes.load(std::memory_order_relaxed),
		state->routingrejections.load(std::memory_order_relaxed),
		state->malformedpackets.load(std::memory_order_relaxed),
		state->ratelimitedpackets.load(std::memory_order_relaxed),
		state->sendfailures.load(std::memory_order_relaxed),
		getPeerCount()
	};
}

std::size_t gTeamVoiceServer::getPeerCount() const {
	std::lock_guard<std::mutex> lock(state->mutex);
	return state->peers.size();
}

void gTeamVoiceServer::reset() {
	std::lock_guard<std::mutex> lock(state->mutex);
	state->peers.clear();
}
