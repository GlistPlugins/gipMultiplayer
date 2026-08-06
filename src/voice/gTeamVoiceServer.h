/*
 * gTeamVoiceServer.h
 *
 * Server-authoritative team voice routing.
 */

#ifndef GTEAMVOICESERVER_H_
#define GTEAMVOICESERVER_H_

#include "voice/gTeamVoicePackets.h"

#include "znet/peer_session.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>


class gTeamVoiceServer {
public:
	using ConnectionId = std::uint64_t;
	using SendCallback = std::function<bool(const std::shared_ptr<znet::Packet>&, const znet::SendOptions&)>;

	struct Config {
		std::size_t maxconnections = 64;
		double packetspersecond = 65.0;
		double packetburst = 20.0;
		double bytespersecond = 24576.0;
		double byteburst = 8192.0;
	};

	struct PeerState {
		std::uint64_t playerid = 0;
		std::uint64_t teamid = 0;
		std::uint64_t sessionid = 0;
		bool cantransmit = false;
		bool canreceive = false;
	};

	struct Stats {
		std::uint64_t receivedpackets;
		std::uint64_t receivedbytes;
		std::uint64_t relayedpackets;
		std::uint64_t relayedbytes;
		std::uint64_t routingrejections;
		std::uint64_t malformedpackets;
		std::uint64_t ratelimitedpackets;
		std::uint64_t sendfailures;
		std::size_t activeconnections;
	};

	gTeamVoiceServer();
	explicit gTeamVoiceServer(const Config& config);
	~gTeamVoiceServer();

	gTeamVoiceServer(const gTeamVoiceServer&) = delete;
	gTeamVoiceServer& operator=(const gTeamVoiceServer&) = delete;

	bool addPeer(ConnectionId connectionid, SendCallback sendcallback);
	bool addPeer(const std::shared_ptr<znet::PeerSession>& session);
	void removePeer(ConnectionId connectionid);

	bool setPeerState(ConnectionId connectionid, const PeerState& state);
	bool clearPeerState(ConnectionId connectionid);

	void handleVoicePacket(ConnectionId senderconnectionid, const gTeamVoiceUplinkPacket& packet);
	void reportMalformedPacket(gTeamVoicePacketError error = gTeamVoicePacketError::BUFFER_ERROR);

	Stats getStats() const;
	std::size_t getPeerCount() const;
	void reset();

private:
	class State;
	std::unique_ptr<State> state;
};

#endif /* GTEAMVOICESERVER_H_ */
