#ifndef VOICEDEMOSERVER_H_
#define VOICEDEMOSERVER_H_

#include "voice/gTeamVoiceServer.h"

#include "znet/server.h"
#include "znet/server_events.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>


class VoiceDemoServerPacketHandler;

class VoiceDemoServer {
	friend class VoiceDemoServerPacketHandler;
public:
	struct Status {
		bool running = false;
		std::string error;
		gTeamVoiceServer::Stats stats{};
	};

	VoiceDemoServer();
	~VoiceDemoServer();

	VoiceDemoServer(const VoiceDemoServer&) = delete;
	VoiceDemoServer& operator=(const VoiceDemoServer&) = delete;

	bool start(const std::string& bindip, std::uint16_t port);
	void update();
	void shutdown();
	Status getStatus() const;

private:
	bool onPeerConnected(znet::ServerClientConnectedEvent& event);
	bool onPeerDisconnected(znet::ServerClientDisconnectedEvent& event);
	void authorizePeer(gTeamVoiceServer::ConnectionId connectionid);
	void handleVoicePacket(gTeamVoiceServer::ConnectionId connectionid, const gTeamVoiceUplinkPacket& packet);
	static std::uint64_t makeSessionId();

	std::unique_ptr<znet::Server> server;
	gTeamVoiceServer router;
	std::uint64_t sessionid = 0;
	mutable std::mutex statemutex;
	std::unordered_set<gTeamVoiceServer::ConnectionId> connectedpeers;
	std::unordered_set<gTeamVoiceServer::ConnectionId> pendingpeers;
	std::chrono::steady_clock::time_point nextretry{};
	std::string error;
	bool running = false;
};

#endif /* VOICEDEMOSERVER_H_ */
