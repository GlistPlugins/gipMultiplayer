/*
 * gVoiceLoopback.cpp
 */

#include "audio/gVoiceLoopback.h"

#include "audio/gVoiceConstants.h"

#include "gSound.h"
#include "gipOpus.h"
#include "miniaudio.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <thread>
#include <vector>


namespace {

constexpr int OPUS_MAX_PACKET_SIZE = 1275;
constexpr int DELAY_MILLISECONDS = 2000;
constexpr int DELAY_FRAMES = gvoice::SAMPLERATE * DELAY_MILLISECONDS / 1000;
constexpr int DELAY_PACKETS = DELAY_FRAMES / gvoice::FRAME_SAMPLES;
constexpr std::size_t WORKER_RING_FRAMES = gvoice::SAMPLERATE;

class AudioRingBuffer {
public:
	explicit AudioRingBuffer(std::size_t capacity) : buffer(capacity), readposition(0), writeposition(0) {
	}

	void reset() {
		readposition.store(0, std::memory_order_relaxed);
		writeposition.store(0, std::memory_order_relaxed);
	}

	std::size_t available() const {
		std::size_t write = writeposition.load(std::memory_order_acquire);
		std::size_t read = readposition.load(std::memory_order_acquire);
		return write - read;
	}

	std::size_t write(const std::int16_t* input, std::size_t frames) {
		std::size_t write = writeposition.load(std::memory_order_relaxed);
		std::size_t read = readposition.load(std::memory_order_acquire);
		std::size_t count = std::min(frames, buffer.size() - (write - read));
		copyIntoBuffer(input, write, count);
		writeposition.store(write + count, std::memory_order_release);
		return count;
	}

	std::size_t read(std::int16_t* output, std::size_t frames) {
		std::size_t read = readposition.load(std::memory_order_relaxed);
		std::size_t write = writeposition.load(std::memory_order_acquire);
		std::size_t count = std::min(frames, write - read);
		copyFromBuffer(output, read, count);
		readposition.store(read + count, std::memory_order_release);
		return count;
	}

private:
	void copyIntoBuffer(const std::int16_t* input, std::size_t position, std::size_t frames) {
		std::size_t offset = position % buffer.size();
		std::size_t first = std::min(frames, buffer.size() - offset);
		std::memcpy(buffer.data() + offset, input, first * sizeof(std::int16_t));
		std::memcpy(buffer.data(), input + first, (frames - first) * sizeof(std::int16_t));
	}

	void copyFromBuffer(std::int16_t* output, std::size_t position, std::size_t frames) {
		std::size_t offset = position % buffer.size();
		std::size_t first = std::min(frames, buffer.size() - offset);
		std::memcpy(output, buffer.data() + offset, first * sizeof(std::int16_t));
		std::memcpy(output + first, buffer.data(), (frames - first) * sizeof(std::int16_t));
	}

	std::vector<std::int16_t> buffer;
	std::atomic<std::size_t> readposition;
	std::atomic<std::size_t> writeposition;
};

struct CompressedFrame {
	std::array<unsigned char, OPUS_MAX_PACKET_SIZE> data;
	int size = 0;
};

}

class gVoiceLoopback::State {
public:
	State() : mode(MODE_RAW_PCM), running(false), deviceinitialized(false), rawbuffer(DELAY_FRAMES, 0), rawposition(0),
			rawframes(0), capturering(WORKER_RING_FRAMES), playbackring(WORKER_RING_FRAMES), playbackready(false), pcmbytes(0),
			opusbytes(0), encodedpackets(0), decodedpackets(0), captureoverruns(0), playbackoverruns(0), playbackunderruns(0),
			codecerrors(0) {
		std::memset(&device, 0, sizeof(device));
	}

	~State() {
		stop();
	}

	bool start(Mode newmode) {
		stop();
		lasterror.clear();
		mode = newmode;
		resetBuffers();
		resetStats();
		if (mode == MODE_OPUS && !codec.setup(gvoice::SAMPLERATE, gvoice::CHANNELS, gvoice::BITRATE)) {
			lasterror = codec.getLastError();
			return false;
		}
		ma_device_config config = ma_device_config_init(ma_device_type_duplex);
		config.capture.format = ma_format_s16;
		config.capture.channels = gvoice::CHANNELS;
		config.playback.format = ma_format_s16;
		config.playback.channels = gvoice::CHANNELS;
		config.sampleRate = gvoice::SAMPLERATE;
		config.periodSizeInFrames = 480;
		config.dataCallback = dataCallback;
		config.notificationCallback = notificationCallback;
		config.pUserData = this;
		ma_device* enginedevice = ma_engine_get_device(gGetSoundEngine());
		ma_context* audiocontext = enginedevice == nullptr ? nullptr : ma_device_get_context(enginedevice);
		if (audiocontext == nullptr) {
			lasterror = "Could not access the GlistEngine audio context";
			codec.close();
			return false;
		}
		ma_result result = ma_device_init(audiocontext, &config, &device);
		if (result != MA_SUCCESS) {
			lasterror = ma_result_description(result);
			codec.close();
			return false;
		}
		deviceinitialized = true;
		running.store(true, std::memory_order_release);
		if (mode == MODE_OPUS) {
			worker = std::thread(&State::workerLoop, this);
		}
		result = ma_device_start(&device);
		if (result != MA_SUCCESS) {
			lasterror = ma_result_description(result);
			stop();
			return false;
		}
		return true;
	}

	void stop() {
		running.store(false, std::memory_order_release);
		if (deviceinitialized) {
			ma_device_stop(&device);
		}
		if (worker.joinable()) {
			worker.join();
		}
		if (deviceinitialized) {
			ma_device_uninit(&device);
			deviceinitialized = false;
		}
		codec.close();
	}

	static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 framecount) {
		State* self = static_cast<State*>(device->pUserData);
		std::int16_t* outputframes = static_cast<std::int16_t*>(output);
		const std::int16_t* inputframes = static_cast<const std::int16_t*>(input);
		std::memset(outputframes, 0, framecount * sizeof(std::int16_t));
		if (!self->running.load(std::memory_order_acquire)) {
			return;
		}
		if (self->mode == MODE_RAW_PCM) {
			self->processRaw(inputframes, outputframes, framecount);
		} else {
			self->processOpusCallback(inputframes, outputframes, framecount);
		}
	}

	static void notificationCallback(const ma_device_notification* notification) {
		if (notification->type != ma_device_notification_type_stopped) {
			return;
		}
		State* self = static_cast<State*>(notification->pDevice->pUserData);
		self->running.store(false, std::memory_order_release);
	}

	void processRaw(const std::int16_t* input, std::int16_t* output, ma_uint32 framecount) {
		for (ma_uint32 i = 0; i < framecount; i++) {
			std::int16_t delayed = rawbuffer[rawposition];
			rawbuffer[rawposition] = input == nullptr ? 0 : input[i];
			if (rawframes >= DELAY_FRAMES) {
				output[i] = delayed;
			}
			rawposition++;
			if (rawposition == rawbuffer.size()) {
				rawposition = 0;
			}
			rawframes++;
		}
		pcmbytes.fetch_add(framecount * sizeof(std::int16_t), std::memory_order_relaxed);
	}

	void processOpusCallback(const std::int16_t* input, std::int16_t* output, ma_uint32 framecount) {
		if (input != nullptr) {
			std::size_t written = capturering.write(input, framecount);
			if (written < framecount) {
				captureoverruns.fetch_add(framecount - written, std::memory_order_relaxed);
			}
		}
		std::size_t read = playbackring.read(output, framecount);
		if (playbackready.load(std::memory_order_acquire) && read < framecount) {
			playbackunderruns.fetch_add(framecount - read, std::memory_order_relaxed);
		}
	}

	void workerLoop() {
		std::array<std::int16_t, gvoice::FRAME_SAMPLES> input;
		std::array<std::int16_t, gvoice::FRAME_SAMPLES> decoded;
		std::deque<CompressedFrame> delayedpackets;
		while (running.load(std::memory_order_acquire)) {
			if (capturering.available() < gvoice::FRAME_SAMPLES) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			capturering.read(input.data(), input.size());
			CompressedFrame frame;
			frame.size = codec.encode(input.data(), gvoice::FRAME_SAMPLES, frame.data.data(), frame.data.size());
			pcmbytes.fetch_add(input.size() * sizeof(std::int16_t), std::memory_order_relaxed);
			if (frame.size < 0) {
				codecerrors.fetch_add(1, std::memory_order_relaxed);
				continue;
			}
			opusbytes.fetch_add(frame.size, std::memory_order_relaxed);
			encodedpackets.fetch_add(1, std::memory_order_relaxed);
			delayedpackets.push_back(frame);
			if (delayedpackets.size() >= DELAY_PACKETS) {
				CompressedFrame& delayed = delayedpackets.front();
				int decodedframes = codec.decode(delayed.data.data(), delayed.size, decoded.data(), decoded.size());
				if (decodedframes < 0) {
					codecerrors.fetch_add(1, std::memory_order_relaxed);
				} else {
					std::size_t written = playbackring.write(decoded.data(), decodedframes);
					if (written < static_cast<std::size_t>(decodedframes)) {
						playbackoverruns.fetch_add(decodedframes - written, std::memory_order_relaxed);
					}
					decodedpackets.fetch_add(1, std::memory_order_relaxed);
					playbackready.store(true, std::memory_order_release);
				}
				delayedpackets.pop_front();
			}
		}
	}

	void resetBuffers() {
		std::fill(rawbuffer.begin(), rawbuffer.end(), 0);
		rawposition = 0;
		rawframes = 0;
		capturering.reset();
		playbackring.reset();
		playbackready.store(false, std::memory_order_relaxed);
	}

	void resetStats() {
		pcmbytes.store(0, std::memory_order_relaxed);
		opusbytes.store(0, std::memory_order_relaxed);
		encodedpackets.store(0, std::memory_order_relaxed);
		decodedpackets.store(0, std::memory_order_relaxed);
		captureoverruns.store(0, std::memory_order_relaxed);
		playbackoverruns.store(0, std::memory_order_relaxed);
		playbackunderruns.store(0, std::memory_order_relaxed);
		codecerrors.store(0, std::memory_order_relaxed);
	}

	Mode mode;
	std::atomic<bool> running;
	bool deviceinitialized;
	ma_device device;
	std::thread worker;
	gipOpus codec;
	std::vector<std::int16_t> rawbuffer;
	std::size_t rawposition;
	std::uint64_t rawframes;
	AudioRingBuffer capturering;
	AudioRingBuffer playbackring;
	std::atomic<bool> playbackready;
	std::atomic<std::uint64_t> pcmbytes;
	std::atomic<std::uint64_t> opusbytes;
	std::atomic<std::uint64_t> encodedpackets;
	std::atomic<std::uint64_t> decodedpackets;
	std::atomic<std::uint64_t> captureoverruns;
	std::atomic<std::uint64_t> playbackoverruns;
	std::atomic<std::uint64_t> playbackunderruns;
	std::atomic<std::uint64_t> codecerrors;
	std::string lasterror;
};

gVoiceLoopback::gVoiceLoopback() : state(std::make_unique<State>()) {
}

gVoiceLoopback::~gVoiceLoopback() {
	stop();
}

bool gVoiceLoopback::start(Mode mode) {
	return state->start(mode);
}

void gVoiceLoopback::stop() {
	state->stop();
}

bool gVoiceLoopback::runCodecSelfTest(std::string& result) {
	gipOpus testcodec;
	if (!testcodec.setup(gvoice::SAMPLERATE, gvoice::CHANNELS, gvoice::BITRATE)) {
		result = testcodec.getLastError();
		return false;
	}
	std::array<std::int16_t, gvoice::FRAME_SAMPLES> input;
	std::array<std::int16_t, gvoice::FRAME_SAMPLES> output;
	std::array<unsigned char, OPUS_MAX_PACKET_SIZE> packet;
	constexpr double VOICE_PI = 3.14159265358979323846;
	for (std::size_t i = 0; i < input.size(); i++) {
		input[i] = static_cast<std::int16_t>(std::sin(2.0 * VOICE_PI * 440.0 * i / gvoice::SAMPLERATE) * 12000.0);
	}
	int encodedbytes = testcodec.encode(input.data(), input.size(), packet.data(), packet.size());
	if (encodedbytes <= 0) {
		result = testcodec.getLastError();
		return false;
	}
	int decodedframes = testcodec.decode(packet.data(), encodedbytes, output.data(), output.size());
	if (decodedframes != gvoice::FRAME_SAMPLES) {
		result = decodedframes < 0 ? testcodec.getLastError() : "Opus decoded an unexpected frame count";
		return false;
	}
	if (encodedbytes >= static_cast<int>(input.size() * sizeof(std::int16_t))) {
		result = "Opus packet is not smaller than the input PCM frame";
		return false;
	}
	result = "passed (" + std::to_string(input.size() * sizeof(std::int16_t)) + " PCM bytes -> " +
			std::to_string(encodedbytes) + " Opus bytes)";
	return true;
}

bool gVoiceLoopback::isRunning() const {
	return state->running.load(std::memory_order_acquire);
}

gVoiceLoopback::Mode gVoiceLoopback::getMode() const {
	return state->mode;
}

gVoiceLoopback::Stats gVoiceLoopback::getStats() const {
	return {
		state->pcmbytes.load(std::memory_order_relaxed),
		state->opusbytes.load(std::memory_order_relaxed),
		state->encodedpackets.load(std::memory_order_relaxed),
		state->decodedpackets.load(std::memory_order_relaxed),
		state->captureoverruns.load(std::memory_order_relaxed),
		state->playbackoverruns.load(std::memory_order_relaxed),
		state->playbackunderruns.load(std::memory_order_relaxed),
		state->codecerrors.load(std::memory_order_relaxed)
	};
}

const std::string& gVoiceLoopback::getLastError() const {
	return state->lasterror;
}

const char* gVoiceLoopback::getModeName(Mode mode) {
	return mode == MODE_RAW_PCM ? "Raw PCM" : "Opus round-trip";
}

int gVoiceLoopback::getDelayMilliseconds() {
	return DELAY_MILLISECONDS;
}
