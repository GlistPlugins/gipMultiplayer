/*
 * gTeamVoice.h
 *
 * Low-latency network team voice client.
 */

#ifndef GTEAMVOICE_H_
#define GTEAMVOICE_H_

#include "audio/gVoiceAudioProcessor.h"
#include "gBaseComponent.h"

#include "znet/peer_session.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>


class gTeamVoice : public gBaseComponent {
public:
	struct Stats : gVoiceAudioProcessor::Stats {
		std::uint64_t sentpackets;
		std::uint64_t sentbytes;
		std::uint64_t sendfailures;
		std::uint64_t rejectedpackets;
		std::uint64_t malformedpackets;
	};

	gTeamVoice();
	virtual ~gTeamVoice();

	gTeamVoice(const gTeamVoice&) = delete;
	gTeamVoice& operator=(const gTeamVoice&) = delete;

	bool initialize();
	void shutdown();
	bool isInitialized() const;

	void setEnabled(bool enabled);
	bool isEnabled() const;
	void startTransmitting();
	void stopTransmitting();
	bool isTransmitting() const;
	void setLocalMuted(bool muted);
	bool isLocalMuted() const;

	bool setSpeakerMuted(std::uint64_t speakerid, bool muted);
	bool setSpeakerVolume(std::uint64_t speakerid, float volume);

	void handleSessionPacket(const gTeamVoiceSessionPacket& packet);
	void handleVoicePacket(const gTeamVoiceDownlinkPacket& packet);
	void reportMalformedPacket(gTeamVoicePacketError error = gTeamVoicePacketError::BUFFER_ERROR);
	void resetSession();

	std::size_t updateNetwork(znet::PeerSession& session);
	std::size_t updateNetwork(const std::function<bool(const gTeamVoiceUplinkPacket&)>& sendCallback);

	Stats getStats() const;
	std::vector<gVoiceAudioProcessor::SpeakerStats> getSpeakerStats() const;
	std::string getLastError() const;

private:
	class State;
	std::unique_ptr<State> state;
};

#endif /* GTEAMVOICE_H_ */
