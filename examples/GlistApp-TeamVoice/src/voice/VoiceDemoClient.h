#ifndef VOICEDEMOCLIENT_H_
#define VOICEDEMOCLIENT_H_

#include "audio/gTeamVoice.h"

#include "znet/client.h"
#include "znet/client_events.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>


class VoiceDemoClientPacketHandler;

class VoiceDemoClient {
	friend class VoiceDemoClientPacketHandler;
public:
	struct Status {
		bool initialized = false;
		bool connected = false;
		bool authorized = false;
		bool transmitting = false;
		std::string error;
		gTeamVoice::Stats stats{};
	};

	VoiceDemoClient();
	~VoiceDemoClient();

	VoiceDemoClient(const VoiceDemoClient&) = delete;
	VoiceDemoClient& operator=(const VoiceDemoClient&) = delete;

	bool start(const std::string& serverip, std::uint16_t port);
	void update();
	void shutdown();

	void startTransmitting();
	void stopTransmitting();
	Status getStatus() const;

private:
	bool onConnected(znet::ClientConnectedToServerEvent& event);
	bool onDisconnected(znet::ClientDisconnectedFromServerEvent& event);
	void handleSessionPacket(const gTeamVoiceSessionPacket& packet);
	void handleVoicePacket(const gTeamVoiceDownlinkPacket& packet);
	void reportMalformedPacket(gTeamVoicePacketError error);
	void setConnectionError(const std::string& error);
	static std::int64_t steadyMilliseconds();

	gTeamVoice voice;
	std::unique_ptr<znet::Client> client;
	std::thread connectionthread;
	mutable std::mutex statemutex;
	std::shared_ptr<znet::PeerSession> session;
	std::string connectionerror;
	std::int64_t connectiondeadlinemilliseconds = 0;
	bool connected = false;
	std::atomic<bool> authorized{false};
	std::atomic<bool> readyqueued{false};
	std::atomic<bool> shuttingdown{false};
};

#endif /* VOICEDEMOCLIENT_H_ */
