/*
 * gVoiceAudioProcessor.h
 *
 * Device-independent worker used by network team voice and deterministic tests.
 */

#ifndef GVOICEAUDIOPROCESSOR_H_
#define GVOICEAUDIOPROCESSOR_H_

#include "voice/gTeamVoicePackets.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


class gVoiceAudioProcessor {
public:
	struct Config {
		std::size_t captureframes = 8;
		std::size_t outgoingpackets = 8;
		std::size_t incomingpackets = 256;
		std::size_t playbackframes = 6;
		std::size_t maxspeakers = 32;
		std::size_t jitterpackets = 12;
		std::size_t initialjitterpackets = 4;
		int maxplcframes = 5;
		int speakertimeoutmilliseconds = 2000;
	};

	struct Stats {
		std::uint64_t capturedframes;
		std::uint64_t captureoverruns;
		std::uint64_t encodedpackets;
		std::uint64_t encodeerrors;
		std::uint64_t outgoingqueuedrops;
		std::uint64_t receivedpackets;
		std::uint64_t receivedbytes;
		std::uint64_t incomingqueuedrops;
		std::uint64_t duplicatepackets;
		std::uint64_t latepackets;
		std::uint64_t reorderedpackets;
		std::uint64_t missingpackets;
		std::uint64_t plcframes;
		std::uint64_t decodedpackets;
		std::uint64_t decodeerrors;
		std::uint64_t mixedframes;
		std::uint64_t playbackoverruns;
		std::uint64_t playbackunderruns;
		std::size_t jitterdepth;
		std::size_t activespeakers;
	};

	struct SpeakerStats {
		std::uint64_t speakerid;
		std::uint64_t receivedpackets;
		std::uint64_t decodedpackets;
		std::uint64_t missingpackets;
		std::uint64_t plcframes;
		std::uint64_t decodeerrors;
		std::uint64_t lastactivitymilliseconds;
		std::size_t jitterdepth;
		bool muted;
		float volume;
	};

	gVoiceAudioProcessor();
	explicit gVoiceAudioProcessor(const Config& config);
	~gVoiceAudioProcessor();

	gVoiceAudioProcessor(const gVoiceAudioProcessor&) = delete;
	gVoiceAudioProcessor& operator=(const gVoiceAudioProcessor&) = delete;

	bool start();
	void stop();
	bool isRunning() const;

	void setEnabled(bool enabled);
	void setTransmitting(bool transmitting);
	void setLocalMuted(bool muted);
	bool isTransmitting() const;

	void setSession(std::uint64_t sessionid, std::uint32_t membershipgeneration, std::uint64_t localplayerid);
	void clearSession();

	void pushCapturedSamples(const std::int16_t* input, std::size_t samples);
	std::size_t popPlaybackSamples(std::int16_t* output, std::size_t samples);

	bool popOutgoingPacket(gTeamVoiceUplinkPacket& packet);
	bool pushIncomingPacket(const gTeamVoiceDownlinkPacket& packet);

	bool setSpeakerMuted(std::uint64_t speakerid, bool muted);
	bool setSpeakerVolume(std::uint64_t speakerid, float volume);

	Stats getStats() const;
	std::vector<SpeakerStats> getSpeakerStats() const;
	std::string getLastError() const;

	static void mixFrames(const std::vector<const std::int16_t*>& inputs, std::size_t samples,
			std::int16_t* output);

private:
	class State;
	std::unique_ptr<State> state;
};

#endif /* GVOICEAUDIOPROCESSOR_H_ */
