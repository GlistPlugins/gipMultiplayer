/*
 * gVoiceLoopback.h
 *
 * Local microphone loopback used to validate delayed PCM and Opus voice audio.
 */

#ifndef GVOICELOOPBACK_H_
#define GVOICELOOPBACK_H_

#include "gBaseComponent.h"

#include <cstdint>
#include <memory>
#include <string>


class gVoiceLoopback : public gBaseComponent {
public:
	enum Mode {
		MODE_RAW_PCM,
		MODE_OPUS
	};

	struct Stats {
		std::uint64_t pcmbytes;
		std::uint64_t opusbytes;
		std::uint64_t encodedpackets;
		std::uint64_t decodedpackets;
		std::uint64_t captureoverruns;
		std::uint64_t playbackoverruns;
		std::uint64_t playbackunderruns;
		std::uint64_t codecerrors;
	};

	gVoiceLoopback();
	virtual ~gVoiceLoopback();

	gVoiceLoopback(const gVoiceLoopback&) = delete;
	gVoiceLoopback& operator=(const gVoiceLoopback&) = delete;

	bool start(Mode mode);
	void stop();
	bool runCodecSelfTest(std::string& result);

	bool isRunning() const;
	Mode getMode() const;
	Stats getStats() const;
	const std::string& getLastError() const;
	static const char* getModeName(Mode mode);
	static int getDelayMilliseconds();

private:
	class State;
	std::unique_ptr<State> state;
};

#endif /* GVOICELOOPBACK_H_ */
