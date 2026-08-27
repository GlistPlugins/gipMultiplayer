/*
 * gVoiceAudioProcessor.cpp
 */

#include "audio/gVoiceAudioProcessor.h"

#include "audio/gVoiceConstants.h"
#include "gipOpus.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>


namespace {

using VoiceClock = std::chrono::steady_clock;
constexpr std::uint32_t MAX_FORWARD_SEQUENCE_JUMP = 250;

template<typename T>
class SpscQueue {
public:
	explicit SpscQueue(std::size_t capacity) : slots(std::max<std::size_t>(1, capacity)) {
	}

	bool push(const T& value) {
		std::size_t write = writeposition.load(std::memory_order_relaxed);
		std::size_t read = readposition.load(std::memory_order_acquire);
		if (write - read >= slots.size()) return false;
		slots[write % slots.size()] = value;
		writeposition.store(write + 1, std::memory_order_release);
		return true;
	}

	bool pop(T& value) {
		std::size_t read = readposition.load(std::memory_order_relaxed);
		std::size_t write = writeposition.load(std::memory_order_acquire);
		if (read == write) return false;
		value = slots[read % slots.size()];
		readposition.store(read + 1, std::memory_order_release);
		return true;
	}

	void discardByConsumer() {
		readposition.store(writeposition.load(std::memory_order_acquire), std::memory_order_release);
	}

private:
	std::vector<T> slots;
	std::atomic<std::size_t> readposition{0};
	std::atomic<std::size_t> writeposition{0};
};

class SampleRingBuffer {
public:
	explicit SampleRingBuffer(std::size_t capacity) : samples(std::max<std::size_t>(1, capacity)) {
	}

	std::size_t write(const std::int16_t* input, std::size_t count) {
		std::size_t write = writeposition.load(std::memory_order_relaxed);
		std::size_t read = readposition.load(std::memory_order_acquire);
		if (count > samples.size() - (write - read)) return 0;
		copyInto(input, write, count);
		writeposition.store(write + count, std::memory_order_release);
		return count;
	}

	std::size_t read(std::int16_t* output, std::size_t count) {
		std::size_t read = readposition.load(std::memory_order_relaxed);
		std::size_t write = writeposition.load(std::memory_order_acquire);
		std::size_t available = std::min(count, write - read);
		copyOut(output, read, available);
		readposition.store(read + available, std::memory_order_release);
		return available;
	}

	void discardByConsumer() {
		readposition.store(writeposition.load(std::memory_order_acquire), std::memory_order_release);
	}

private:
	void copyInto(const std::int16_t* input, std::size_t position, std::size_t count) {
		std::size_t offset = position % samples.size();
		std::size_t first = std::min(count, samples.size() - offset);
		std::memcpy(samples.data() + offset, input, first * sizeof(std::int16_t));
		std::memcpy(samples.data(), input + first, (count - first) * sizeof(std::int16_t));
	}

	void copyOut(std::int16_t* output, std::size_t position, std::size_t count) {
		std::size_t offset = position % samples.size();
		std::size_t first = std::min(count, samples.size() - offset);
		std::memcpy(output, samples.data() + offset, first * sizeof(std::int16_t));
		std::memcpy(output + first, samples.data(), (count - first) * sizeof(std::int16_t));
	}

	std::vector<std::int16_t> samples;
	std::atomic<std::size_t> readposition{0};
	std::atomic<std::size_t> writeposition{0};
};

struct PcmFrame {
	std::array<std::int16_t, gvoice::FRAME_SAMPLES> samples{};
	std::uint64_t sessionepoch = 0;
	std::uint64_t captureepoch = 0;
	std::uint64_t sampleposition = 0;
	bool discontinuity = false;
};

struct EncodedFrame {
	std::uint8_t flags = 0;
	std::uint64_t sessionepoch = 0;
	std::uint64_t captureepoch = 0;
	std::uint64_t sessionid = 0;
	std::uint32_t membershipgeneration = 0;
	std::uint32_t streamgeneration = 0;
	std::uint32_t sequence = 0;
	std::uint64_t sampleposition = 0;
	std::array<unsigned char, gvoice::NETWORK_MAX_OPUS_BYTES> payload{};
	std::uint16_t payloadsize = 0;
};

struct RemoteFrame {
	std::uint8_t flags = 0;
	std::uint64_t sessionepoch = 0;
	std::uint64_t speakerid = 0;
	std::uint32_t speakergeneration = 0;
	std::uint32_t streamgeneration = 0;
	std::uint32_t sequence = 0;
	std::uint64_t sampleposition = 0;
	std::array<unsigned char, gvoice::NETWORK_MAX_OPUS_BYTES> payload{};
	std::uint16_t payloadsize = 0;
};

}

class gVoiceAudioProcessor::State {
public:
	static Config sanitizeConfig(Config config) {
		config.captureframes = std::max<std::size_t>(1, std::min<std::size_t>(config.captureframes, 256));
		config.outgoingpackets = std::max<std::size_t>(1, std::min<std::size_t>(config.outgoingpackets, 1024));
		config.incomingpackets = std::max<std::size_t>(1, std::min<std::size_t>(config.incomingpackets, 4096));
		config.playbackframes = std::max<std::size_t>(1, std::min<std::size_t>(config.playbackframes, 256));
		config.maxspeakers = std::max<std::size_t>(1, std::min<std::size_t>(config.maxspeakers, 256));
		config.jitterpackets = std::max<std::size_t>(1, std::min<std::size_t>(config.jitterpackets, 256));
		config.initialjitterpackets = std::max<std::size_t>(1,
				std::min(config.initialjitterpackets, config.jitterpackets));
		config.maxplcframes = std::max(0, std::min(config.maxplcframes, 100));
		config.speakertimeoutmilliseconds = std::max(gvoice::FRAME_MILLISECONDS,
				std::min(config.speakertimeoutmilliseconds, 60000));
		return config;
	}

	explicit State(const Config& requestedconfig)
			: config(sanitizeConfig(requestedconfig)), capturequeue(config.captureframes),
			  playbackring(config.playbackframes * static_cast<std::size_t>(gvoice::FRAME_SAMPLES)) {
	}

	struct SpeakerControl {
		bool muted = false;
		float volume = 1.0f;
	};

	struct Speaker {
		std::uint64_t speakerid = 0;
		std::uint32_t speakergeneration = 0;
		std::uint32_t streamgeneration = 0;
		std::unique_ptr<gipOpus> codec;
		std::vector<RemoteFrame> jitter;
		bool playing = false;
		bool hasexpected = false;
		std::uint32_t expectedsequence = 0;
		bool hashighest = false;
		std::uint32_t highestsequence = 0;
		int consecutivemissing = 0;
		VoiceClock::time_point firstarrival = VoiceClock::now();
		VoiceClock::time_point lastactivity = VoiceClock::now();
		VoiceClock::time_point nextplayout = VoiceClock::now();
		std::uint64_t receivedpackets = 0;
		std::uint64_t decodedpackets = 0;
		std::uint64_t missingpackets = 0;
		std::uint64_t plcframes = 0;
		std::uint64_t decodeerrors = 0;
	};

	bool start() {
		std::lock_guard<std::mutex> lock(lifecyclemutex);
		if (running.load(std::memory_order_acquire)) return true;
		if (!encodercodec.setup(gvoice::SAMPLERATE, gvoice::CHANNELS, gvoice::BITRATE)) {
			setError(encodercodec.getLastError());
			return false;
		}
		captureepoch.fetch_add(1, std::memory_order_acq_rel);
		running.store(true, std::memory_order_release);
		worker = std::thread(&State::workerLoop, this);
		return true;
	}

	void stop() {
		std::lock_guard<std::mutex> lock(lifecyclemutex);
		running.store(false, std::memory_order_release);
		captureepoch.fetch_add(1, std::memory_order_acq_rel);
		if (worker.joinable()) worker.join();
		encodercodec.close();
		clearWorkerState();
		clearOutgoing();
		clearIncoming();
	}

	void setError(const std::string& error) {
		std::lock_guard<std::mutex> lock(errormutex);
		lasterror = error;
	}

	std::string getError() const {
		std::lock_guard<std::mutex> lock(errormutex);
		return lasterror;
	}

	void clearOutgoing() {
		std::lock_guard<std::mutex> lock(outgoingmutex);
		outgoing.clear();
	}

	void clearIncoming() {
		std::lock_guard<std::mutex> lock(incomingmutex);
		incoming.clear();
	}

	bool captureActive() const {
		return enabled.load(std::memory_order_relaxed) && transmitting.load(std::memory_order_relaxed) &&
				!localmuted.load(std::memory_order_relaxed) && sessionid.load(std::memory_order_relaxed) != 0 &&
				membershipgeneration.load(std::memory_order_relaxed) != 0;
	}

	void pushCaptured(const std::int16_t* input, std::size_t samplecount) {
		if (!input || samplecount == 0) return;
		std::uint64_t currentsessionepoch = sessionepoch.load(std::memory_order_acquire);
		std::uint64_t currentcaptureepoch = captureepoch.load(std::memory_order_acquire);
		if (!running.load(std::memory_order_acquire) || (currentsessionepoch & 1U) != 0 || !captureActive()) {
			capturestagingcount = 0;
			captureposition += samplecount;
			return;
		}
		if (capturestagingcount != 0 && (capturestagingsessionepoch != currentsessionepoch ||
				capturestagingepoch != currentcaptureepoch)) {
			capturestagingcount = 0;
			capturediscontinuity = true;
		}
		std::size_t offset = 0;
		while (offset < samplecount) {
			if (capturestagingcount == 0) {
				capturestagingposition = captureposition + offset;
				capturestagingsessionepoch = currentsessionepoch;
				capturestagingepoch = currentcaptureepoch;
			}
			std::size_t copy = std::min(samplecount - offset,
					static_cast<std::size_t>(gvoice::FRAME_SAMPLES) - capturestagingcount);
			std::memcpy(capturestaging.data() + capturestagingcount, input + offset, copy * sizeof(std::int16_t));
			capturestagingcount += copy;
			offset += copy;
			if (capturestagingcount == static_cast<std::size_t>(gvoice::FRAME_SAMPLES)) {
				PcmFrame frame;
				frame.samples = capturestaging;
				frame.sessionepoch = capturestagingsessionepoch;
				frame.captureepoch = capturestagingepoch;
				frame.sampleposition = capturestagingposition;
				frame.discontinuity = capturediscontinuity;
				if (capturequeue.push(frame)) {
					capturedframes.fetch_add(1, std::memory_order_relaxed);
					capturediscontinuity = false;
				} else {
					captureoverruns.fetch_add(1, std::memory_order_relaxed);
					capturediscontinuity = true;
				}
				capturestagingcount = 0;
			}
		}
		captureposition += samplecount;
	}

	std::size_t popPlayback(std::int16_t* output, std::size_t samplecount) {
		if (!output || samplecount == 0) return 0;
		if (playbackclearrequested.exchange(false, std::memory_order_acq_rel)) {
			playbackring.discardByConsumer();
		}
		std::size_t read = playbackring.read(output, samplecount);
		if (read < samplecount) {
			std::memset(output + read, 0, (samplecount - read) * sizeof(std::int16_t));
			if (playbackactive.load(std::memory_order_relaxed)) {
				playbackunderruns.fetch_add(1, std::memory_order_relaxed);
			}
		}
		return read;
	}

	void workerLoop() {
		std::uint64_t observedsessionepoch = sessionepoch.load(std::memory_order_acquire);
		std::uint64_t observedcaptureepoch = captureepoch.load(std::memory_order_acquire);
		std::uint32_t sequence = 0;
		bool firstpacket = true;
		if ((observedsessionepoch & 1U) == 0 && captureActive()) {
			streamgeneration++;
			if (streamgeneration == 0) streamgeneration++;
		}
		auto nextmix = VoiceClock::now();
		while (running.load(std::memory_order_acquire)) {
			{
				std::lock_guard<std::mutex> worklock(workmutex);
				if (!running.load(std::memory_order_acquire)) break;
				std::uint64_t currentsessionepoch = sessionepoch.load(std::memory_order_acquire);
				if ((currentsessionepoch & 1U) == 0) {
					if (currentsessionepoch != observedsessionepoch) {
						observedsessionepoch = currentsessionepoch;
						clearWorkerState();
						nextmix = VoiceClock::now();
					}
					std::uint64_t currentcaptureepoch = captureepoch.load(std::memory_order_acquire);
					if (currentcaptureepoch != observedcaptureepoch) {
						observedcaptureepoch = currentcaptureepoch;
						capturequeue.discardByConsumer();
						if (captureActive()) {
							streamgeneration++;
							if (streamgeneration == 0) streamgeneration++;
							sequence = 0;
							firstpacket = true;
						}
					}

					processIncoming(observedsessionepoch);
					if (captureActive()) {
						PcmFrame frame;
						while (capturequeue.pop(frame)) {
							if (frame.sessionepoch != observedsessionepoch || frame.captureepoch != observedcaptureepoch) continue;
							encodeFrame(frame, streamgeneration, sequence, firstpacket);
							sequence++;
							firstpacket = false;
						}
					} else {
						capturequeue.discardByConsumer();
					}

					auto now = VoiceClock::now();
					if (now >= nextmix) {
						mixOneFrame(now);
						nextmix += std::chrono::milliseconds(gvoice::FRAME_MILLISECONDS);
						if (now - nextmix > std::chrono::milliseconds(100)) nextmix = now;
					}
					cleanupSpeakers(now);
					updateSnapshots(now);
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	void encodeFrame(const PcmFrame& frame, std::uint32_t streamgeneration, std::uint32_t sequence, bool firstpacket) {
		if (streamgeneration == 0 || frame.sessionepoch != sessionepoch.load(std::memory_order_acquire) ||
				frame.captureepoch != captureepoch.load(std::memory_order_acquire)) return;
		EncodedFrame encoded;
		encoded.sessionepoch = frame.sessionepoch;
		encoded.captureepoch = frame.captureepoch;
		encoded.sessionid = sessionid.load(std::memory_order_acquire);
		encoded.membershipgeneration = membershipgeneration.load(std::memory_order_acquire);
		int bytes = encodercodec.encode(frame.samples.data(), gvoice::FRAME_SAMPLES, encoded.payload.data(),
				static_cast<int>(encoded.payload.size()));
		if (bytes <= 0) {
			encodeerrors.fetch_add(1, std::memory_order_relaxed);
			setError(encodercodec.getLastError());
			return;
		}
		encoded.flags = firstpacket ? G_TEAM_VOICE_FLAG_STREAM_START : G_TEAM_VOICE_FLAG_NONE;
		if (frame.discontinuity) encoded.flags |= G_TEAM_VOICE_FLAG_DISCONTINUITY;
		encoded.streamgeneration = streamgeneration;
		encoded.sequence = sequence;
		encoded.sampleposition = frame.sampleposition;
		encoded.payloadsize = static_cast<std::uint16_t>(bytes);
		{
			std::lock_guard<std::mutex> lock(outgoingmutex);
			if (!running.load(std::memory_order_acquire) || !captureActive() ||
					encoded.sessionepoch != sessionepoch.load(std::memory_order_acquire) ||
					encoded.captureepoch != captureepoch.load(std::memory_order_acquire)) return;
			if (outgoing.size() >= config.outgoingpackets) {
				outgoing.pop_front();
				outgoingqueuedrops.fetch_add(1, std::memory_order_relaxed);
				if (outgoing.empty()) {
					encoded.flags |= G_TEAM_VOICE_FLAG_DISCONTINUITY;
				} else {
					outgoing.front().flags |= G_TEAM_VOICE_FLAG_DISCONTINUITY;
				}
			}
			outgoing.push_back(encoded);
		}
		encodedpackets.fetch_add(1, std::memory_order_relaxed);
	}

	void processIncoming(std::uint64_t observedsessionepoch) {
		std::deque<RemoteFrame> batch;
		{
			std::lock_guard<std::mutex> lock(incomingmutex);
			batch.swap(incoming);
		}
		for (const RemoteFrame& frame : batch) {
			if (frame.sessionepoch == observedsessionepoch &&
					frame.sessionepoch == sessionepoch.load(std::memory_order_acquire) &&
					enabled.load(std::memory_order_acquire)) {
				addRemoteFrame(frame);
			}
		}
	}

	void addRemoteFrame(const RemoteFrame& frame) {
		auto now = VoiceClock::now();
		auto it = speakers.find(frame.speakerid);
		if (it == speakers.end()) {
			if (speakers.size() >= config.maxspeakers) {
				latepackets.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			Speaker speaker;
			speaker.speakerid = frame.speakerid;
			if (!resetSpeaker(speaker, frame, now)) return;
			it = speakers.emplace(frame.speakerid, std::move(speaker)).first;
		} else if (frame.speakergeneration != it->second.speakergeneration ||
				frame.streamgeneration != it->second.streamgeneration ||
				(frame.flags & G_TEAM_VOICE_FLAG_DISCONTINUITY) != 0) {
			if (frame.speakergeneration != it->second.speakergeneration &&
					!gIsTeamVoiceSequenceNewer(frame.speakergeneration, it->second.speakergeneration)) {
				latepackets.fetch_add(1, std::memory_order_relaxed);
				return;
			}
			if (frame.speakergeneration == it->second.speakergeneration &&
					frame.streamgeneration != it->second.streamgeneration &&
					!gIsTeamVoiceSequenceNewer(frame.streamgeneration, it->second.streamgeneration)) {
					latepackets.fetch_add(1, std::memory_order_relaxed);
					return;
			}
			if (frame.speakergeneration == it->second.speakergeneration &&
					frame.streamgeneration == it->second.streamgeneration && it->second.hashighest &&
					!gIsTeamVoiceSequenceNewer(frame.sequence, it->second.highestsequence)) {
				if (frame.sequence == it->second.highestsequence) {
					duplicatepackets.fetch_add(1, std::memory_order_relaxed);
				} else {
					latepackets.fetch_add(1, std::memory_order_relaxed);
				}
				return;
			}
			if (!resetSpeaker(it->second, frame, now)) return;
		}

		Speaker& speaker = it->second;
		if (speaker.hasexpected && frame.sequence != speaker.expectedsequence &&
				!gIsTeamVoiceSequenceNewer(frame.sequence, speaker.expectedsequence)) {
			latepackets.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		if (speaker.hasexpected && gIsTeamVoiceSequenceNewer(frame.sequence, speaker.expectedsequence) &&
				gTeamVoiceSequenceDistance(frame.sequence, speaker.expectedsequence) > MAX_FORWARD_SEQUENCE_JUMP) {
			latepackets.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		for (const RemoteFrame& queued : speaker.jitter) {
			if (queued.sequence == frame.sequence) {
				duplicatepackets.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		}
		if (speaker.hashighest && !gIsTeamVoiceSequenceNewer(frame.sequence, speaker.highestsequence)) {
			reorderedpackets.fetch_add(1, std::memory_order_relaxed);
		}
		if (!speaker.hashighest || gIsTeamVoiceSequenceNewer(frame.sequence, speaker.highestsequence)) {
			speaker.highestsequence = frame.sequence;
			speaker.hashighest = true;
		}
		if (speaker.jitter.size() >= config.jitterpackets) {
			auto oldest = speaker.jitter.begin();
			for (auto candidate = speaker.jitter.begin() + 1; candidate != speaker.jitter.end(); ++candidate) {
				if (gIsTeamVoiceSequenceNewer(oldest->sequence, candidate->sequence)) oldest = candidate;
			}
			if (oldest != speaker.jitter.end() && gIsTeamVoiceSequenceNewer(frame.sequence, oldest->sequence)) {
				*oldest = frame;
			} else {
				latepackets.fetch_add(1, std::memory_order_relaxed);
				return;
			}
		} else {
			speaker.jitter.push_back(frame);
		}
		speaker.lastactivity = now;
		speaker.receivedpackets++;
	}

	bool resetSpeaker(Speaker& speaker, const RemoteFrame& frame, VoiceClock::time_point now) {
		auto codec = std::make_unique<gipOpus>();
		if (!codec->setup(gvoice::SAMPLERATE, gvoice::CHANNELS, gvoice::BITRATE)) {
			decodeerrors.fetch_add(1, std::memory_order_relaxed);
			setError(codec->getLastError());
			return false;
		}
		speaker.codec = std::move(codec);
		speaker.speakergeneration = frame.speakergeneration;
		speaker.streamgeneration = frame.streamgeneration;
		speaker.jitter.clear();
		speaker.playing = false;
		speaker.hasexpected = false;
		speaker.hashighest = false;
		speaker.consecutivemissing = 0;
		speaker.firstarrival = now;
		speaker.lastactivity = now;
		return true;
	}

	void mixOneFrame(VoiceClock::time_point now) {
		std::vector<std::array<std::int16_t, gvoice::FRAME_SAMPLES>> decodedframes;
		std::vector<const std::int16_t*> inputs;
		decodedframes.reserve(speakers.size());
		inputs.reserve(speakers.size());
		for (auto& item : speakers) {
			Speaker& speaker = item.second;
			if (!speaker.playing) {
				if (speaker.jitter.size() < config.initialjitterpackets &&
						now - speaker.firstarrival < std::chrono::milliseconds(gvoice::FRAME_MILLISECONDS *
						static_cast<int>(config.initialjitterpackets))) {
					continue;
				}
				if (speaker.jitter.empty()) continue;
				auto oldest = speaker.jitter.begin();
				for (auto candidate = speaker.jitter.begin() + 1; candidate != speaker.jitter.end(); ++candidate) {
					if (gIsTeamVoiceSequenceNewer(oldest->sequence, candidate->sequence)) oldest = candidate;
				}
				speaker.expectedsequence = oldest->sequence;
				speaker.hasexpected = true;
				speaker.playing = true;
				speaker.nextplayout = now;
			}
			if (now < speaker.nextplayout) continue;

			decodedframes.emplace_back();
			auto& decoded = decodedframes.back();
			auto frame = std::find_if(speaker.jitter.begin(), speaker.jitter.end(), [&speaker](const RemoteFrame& queued) {
				return queued.sequence == speaker.expectedsequence;
			});
			int decodedsamples = 0;
			if (frame != speaker.jitter.end()) {
				decodedsamples = speaker.codec->decode(frame->payload.data(), frame->payloadsize, decoded.data(),
						gvoice::FRAME_SAMPLES);
				speaker.jitter.erase(frame);
				speaker.consecutivemissing = 0;
				if (decodedsamples == gvoice::FRAME_SAMPLES) {
					speaker.decodedpackets++;
					decodedpackets.fetch_add(1, std::memory_order_relaxed);
				}
			} else if (speaker.consecutivemissing < config.maxplcframes) {
				decodedsamples = speaker.codec->decode(nullptr, 0, decoded.data(), gvoice::FRAME_SAMPLES);
				speaker.consecutivemissing++;
				speaker.missingpackets++;
				missingpackets.fetch_add(1, std::memory_order_relaxed);
				if (decodedsamples == gvoice::FRAME_SAMPLES) {
					speaker.plcframes++;
					plcframes.fetch_add(1, std::memory_order_relaxed);
				}
			} else {
				decodedframes.pop_back();
				speaker.playing = false;
				speaker.hasexpected = false;
				continue;
			}

			if (decodedsamples != gvoice::FRAME_SAMPLES) {
				speaker.decodeerrors++;
				decodeerrors.fetch_add(1, std::memory_order_relaxed);
				decodedframes.pop_back();
			} else {
				SpeakerControl control = getSpeakerControl(speaker.speakerid);
				if (!control.muted) {
					if (control.volume != 1.0f) {
						for (std::int16_t& sample : decoded) {
							float scaled = static_cast<float>(sample) * control.volume;
							scaled = std::max(-32768.0f, std::min(32767.0f, scaled));
							sample = static_cast<std::int16_t>(scaled);
						}
					}
					inputs.push_back(decoded.data());
				}
			}
			speaker.expectedsequence++;
			speaker.nextplayout += std::chrono::milliseconds(gvoice::FRAME_MILLISECONDS);
		}

		if (inputs.empty()) {
			playbackactive.store(false, std::memory_order_relaxed);
			return;
		}
		std::array<std::int16_t, gvoice::FRAME_SAMPLES> mixed{};
		gVoiceAudioProcessor::mixFrames(inputs, mixed.size(), mixed.data());
		if (playbackring.write(mixed.data(), mixed.size()) != mixed.size()) {
			playbackoverruns.fetch_add(1, std::memory_order_relaxed);
		} else {
			mixedframes.fetch_add(1, std::memory_order_relaxed);
			playbackactive.store(true, std::memory_order_release);
		}
	}

	void cleanupSpeakers(VoiceClock::time_point now) {
		for (auto it = speakers.begin(); it != speakers.end();) {
			if (now - it->second.lastactivity > std::chrono::milliseconds(config.speakertimeoutmilliseconds)) {
				it = speakers.erase(it);
			} else {
				++it;
			}
		}
		activespeakers.store(speakers.size(), std::memory_order_relaxed);
		std::size_t depth = 0;
		for (const auto& item : speakers) depth += item.second.jitter.size();
		jitterdepth.store(depth, std::memory_order_relaxed);
	}

	void clearWorkerState() {
		speakers.clear();
		capturequeue.discardByConsumer();
		playbackclearrequested.store(true, std::memory_order_release);
		playbackactive.store(false, std::memory_order_relaxed);
		activespeakers.store(0, std::memory_order_relaxed);
		jitterdepth.store(0, std::memory_order_relaxed);
		std::lock_guard<std::mutex> lock(snapshotmutex);
		speakersnapshot.clear();
	}

	SpeakerControl getSpeakerControl(std::uint64_t speakerid) const {
		std::lock_guard<std::mutex> lock(controlmutex);
		auto it = speakercontrols.find(speakerid);
		return it == speakercontrols.end() ? SpeakerControl{} : it->second;
	}

	bool setControl(std::uint64_t speakerid, bool setmute, bool muted, bool setvolume, float volume) {
		if (speakerid == 0) return false;
		std::lock_guard<std::mutex> lock(controlmutex);
		auto it = speakercontrols.find(speakerid);
		if (it == speakercontrols.end()) {
			if (speakercontrols.size() >= config.maxspeakers) return false;
			it = speakercontrols.emplace(speakerid, SpeakerControl{}).first;
		}
		if (setmute) it->second.muted = muted;
		if (setvolume) it->second.volume = std::max(0.0f, std::min(2.0f, volume));
		return true;
	}

	void updateSnapshots(VoiceClock::time_point now) {
		std::vector<SpeakerStats> snapshot;
		snapshot.reserve(speakers.size());
		for (const auto& item : speakers) {
			const Speaker& speaker = item.second;
			SpeakerControl control = getSpeakerControl(speaker.speakerid);
			snapshot.push_back({
				speaker.speakerid,
				speaker.receivedpackets,
				speaker.decodedpackets,
				speaker.missingpackets,
				speaker.plcframes,
				speaker.decodeerrors,
				static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - speaker.lastactivity).count()),
				speaker.jitter.size(),
				control.muted,
				control.volume
			});
		}
		std::lock_guard<std::mutex> lock(snapshotmutex);
		speakersnapshot.swap(snapshot);
	}

	Config config;
	mutable std::mutex lifecyclemutex;
	mutable std::mutex transitionmutex;
	mutable std::mutex workmutex;
	mutable std::mutex errormutex;
	mutable std::mutex outgoingmutex;
	mutable std::mutex incomingmutex;
	mutable std::mutex controlmutex;
	mutable std::mutex snapshotmutex;
	std::atomic<bool> running{false};
	std::atomic<bool> enabled{true};
	std::atomic<bool> transmitting{false};
	std::atomic<bool> localmuted{false};
	std::atomic<std::uint64_t> sessionid{0};
	std::atomic<std::uint32_t> membershipgeneration{0};
	std::atomic<std::uint64_t> localplayerid{0};
	std::atomic<std::uint64_t> sessionepoch{2};
	std::atomic<std::uint64_t> captureepoch{1};
	std::thread worker;
	gipOpus encodercodec;
	std::uint32_t streamgeneration = 0;
	SpscQueue<PcmFrame> capturequeue;
	SampleRingBuffer playbackring;
	std::array<std::int16_t, gvoice::FRAME_SAMPLES> capturestaging{};
	std::size_t capturestagingcount = 0;
	std::uint64_t capturestagingposition = 0;
	std::uint64_t capturestagingsessionepoch = 0;
	std::uint64_t capturestagingepoch = 0;
	std::uint64_t captureposition = 0;
	bool capturediscontinuity = false;
	std::deque<EncodedFrame> outgoing;
	std::deque<RemoteFrame> incoming;
	std::unordered_map<std::uint64_t, Speaker> speakers;
	std::unordered_map<std::uint64_t, SpeakerControl> speakercontrols;
	std::vector<SpeakerStats> speakersnapshot;
	std::string lasterror;
	std::atomic<bool> playbackactive{false};
	std::atomic<bool> playbackclearrequested{false};
	std::atomic<std::uint64_t> capturedframes{0};
	std::atomic<std::uint64_t> captureoverruns{0};
	std::atomic<std::uint64_t> encodedpackets{0};
	std::atomic<std::uint64_t> encodeerrors{0};
	std::atomic<std::uint64_t> outgoingqueuedrops{0};
	std::atomic<std::uint64_t> receivedpackets{0};
	std::atomic<std::uint64_t> receivedbytes{0};
	std::atomic<std::uint64_t> incomingqueuedrops{0};
	std::atomic<std::uint64_t> duplicatepackets{0};
	std::atomic<std::uint64_t> latepackets{0};
	std::atomic<std::uint64_t> reorderedpackets{0};
	std::atomic<std::uint64_t> missingpackets{0};
	std::atomic<std::uint64_t> plcframes{0};
	std::atomic<std::uint64_t> decodedpackets{0};
	std::atomic<std::uint64_t> decodeerrors{0};
	std::atomic<std::uint64_t> mixedframes{0};
	std::atomic<std::uint64_t> playbackoverruns{0};
	std::atomic<std::uint64_t> playbackunderruns{0};
	std::atomic<std::size_t> jitterdepth{0};
	std::atomic<std::size_t> activespeakers{0};
};

gVoiceAudioProcessor::gVoiceAudioProcessor() : state(std::make_unique<State>(Config{})) {
}

gVoiceAudioProcessor::gVoiceAudioProcessor(const Config& config) : state(std::make_unique<State>(config)) {
}

gVoiceAudioProcessor::~gVoiceAudioProcessor() {
	stop();
}

bool gVoiceAudioProcessor::start() {
	return state->start();
}

void gVoiceAudioProcessor::stop() {
	state->stop();
}

bool gVoiceAudioProcessor::isRunning() const {
	return state->running.load(std::memory_order_acquire);
}

void gVoiceAudioProcessor::setEnabled(bool enabled) {
	std::lock_guard<std::mutex> transitionlock(state->transitionmutex);
	if (state->enabled.load(std::memory_order_acquire) == enabled) return;
	std::lock_guard<std::mutex> worklock(state->workmutex);
	state->sessionepoch.fetch_add(1, std::memory_order_acq_rel);
	state->enabled.store(enabled, std::memory_order_release);
	state->captureepoch.fetch_add(1, std::memory_order_acq_rel);
	state->sessionepoch.fetch_add(1, std::memory_order_release);
	state->clearOutgoing();
	state->clearIncoming();
	state->playbackclearrequested.store(true, std::memory_order_release);
}

void gVoiceAudioProcessor::setTransmitting(bool transmitting) {
	if (state->transmitting.load(std::memory_order_acquire) == transmitting) return;
	state->transmitting.store(transmitting, std::memory_order_release);
	state->captureepoch.fetch_add(1, std::memory_order_acq_rel);
	state->clearOutgoing();
}

void gVoiceAudioProcessor::setLocalMuted(bool muted) {
	if (state->localmuted.load(std::memory_order_acquire) == muted) return;
	state->localmuted.store(muted, std::memory_order_release);
	state->captureepoch.fetch_add(1, std::memory_order_acq_rel);
	state->clearOutgoing();
}

bool gVoiceAudioProcessor::isTransmitting() const {
	return state->transmitting.load(std::memory_order_acquire);
}

void gVoiceAudioProcessor::setSession(std::uint64_t sessionid, std::uint32_t membershipgeneration,
		std::uint64_t localplayerid) {
	std::lock_guard<std::mutex> transitionlock(state->transitionmutex);
	std::lock_guard<std::mutex> worklock(state->workmutex);
	state->sessionepoch.fetch_add(1, std::memory_order_acq_rel);
	state->sessionid.store(sessionid, std::memory_order_release);
	state->membershipgeneration.store(membershipgeneration, std::memory_order_release);
	state->localplayerid.store(localplayerid, std::memory_order_release);
	state->captureepoch.fetch_add(1, std::memory_order_acq_rel);
	state->sessionepoch.fetch_add(1, std::memory_order_release);
	state->clearOutgoing();
	state->clearIncoming();
	{
		std::lock_guard<std::mutex> lock(state->controlmutex);
		state->speakercontrols.clear();
	}
	state->playbackclearrequested.store(true, std::memory_order_release);
}

void gVoiceAudioProcessor::clearSession() {
	setSession(0, 0, 0);
}

void gVoiceAudioProcessor::pushCapturedSamples(const std::int16_t* input, std::size_t samples) {
	state->pushCaptured(input, samples);
}

std::size_t gVoiceAudioProcessor::popPlaybackSamples(std::int16_t* output, std::size_t samples) {
	return state->popPlayback(output, samples);
}

bool gVoiceAudioProcessor::popOutgoingPacket(gTeamVoiceUplinkPacket& packet) {
	if (!state->running.load(std::memory_order_acquire)) return false;
	EncodedFrame encoded;
	while (true) {
		{
			std::lock_guard<std::mutex> lock(state->outgoingmutex);
			if (!state->captureActive()) return false;
			if (state->outgoing.empty()) return false;
			encoded = state->outgoing.front();
			state->outgoing.pop_front();
		}
		if (encoded.sessionepoch == state->sessionepoch.load(std::memory_order_acquire) &&
				encoded.captureepoch == state->captureepoch.load(std::memory_order_acquire) &&
				encoded.sessionid == state->sessionid.load(std::memory_order_acquire) &&
				encoded.membershipgeneration == state->membershipgeneration.load(std::memory_order_acquire)) break;
	}
	packet.version = G_TEAM_VOICE_PROTOCOL_VERSION;
	packet.flags = encoded.flags;
	packet.sessionid = encoded.sessionid;
	packet.membershipgeneration = encoded.membershipgeneration;
	packet.streamgeneration = encoded.streamgeneration;
	packet.sequence = encoded.sequence;
	packet.sampleposition = encoded.sampleposition;
	packet.payload.assign(encoded.payload.begin(), encoded.payload.begin() + encoded.payloadsize);
	return true;
}

bool gVoiceAudioProcessor::pushIncomingPacket(const gTeamVoiceDownlinkPacket& packet) {
	if (gValidateTeamVoiceDownlinkPacket(packet) != gTeamVoicePacketError::NONE) return false;
	std::uint64_t currentsessionepoch = state->sessionepoch.load(std::memory_order_acquire);
	if ((currentsessionepoch & 1U) != 0 || !state->running.load(std::memory_order_acquire) ||
			packet.sessionid != state->sessionid.load(std::memory_order_acquire) ||
			packet.recipientgeneration != state->membershipgeneration.load(std::memory_order_acquire) ||
			packet.speakerid == state->localplayerid.load(std::memory_order_acquire) ||
			!state->enabled.load(std::memory_order_acquire)) {
		return false;
	}
	RemoteFrame remote;
	remote.flags = packet.flags;
	remote.sessionepoch = currentsessionepoch;
	remote.speakerid = packet.speakerid;
	remote.speakergeneration = packet.speakergeneration;
	remote.streamgeneration = packet.streamgeneration;
	remote.sequence = packet.sequence;
	remote.sampleposition = packet.sampleposition;
	remote.payloadsize = static_cast<std::uint16_t>(packet.payload.size());
	std::copy(packet.payload.begin(), packet.payload.end(), remote.payload.begin());
	{
		std::lock_guard<std::mutex> lock(state->incomingmutex);
		if (!state->running.load(std::memory_order_acquire) || !state->enabled.load(std::memory_order_acquire) ||
				remote.sessionepoch != state->sessionepoch.load(std::memory_order_acquire) ||
				packet.sessionid != state->sessionid.load(std::memory_order_acquire) ||
				packet.recipientgeneration != state->membershipgeneration.load(std::memory_order_acquire)) {
			return false;
		}
		if (state->incoming.size() >= state->config.incomingpackets) {
			state->incoming.pop_front();
			state->incomingqueuedrops.fetch_add(1, std::memory_order_relaxed);
		}
		state->incoming.push_back(remote);
	}
	state->receivedpackets.fetch_add(1, std::memory_order_relaxed);
	state->receivedbytes.fetch_add(packet.payload.size(), std::memory_order_relaxed);
	return true;
}

bool gVoiceAudioProcessor::setSpeakerMuted(std::uint64_t speakerid, bool muted) {
	return state->setControl(speakerid, true, muted, false, 1.0f);
}

bool gVoiceAudioProcessor::setSpeakerVolume(std::uint64_t speakerid, float volume) {
	return state->setControl(speakerid, false, false, true, volume);
}

gVoiceAudioProcessor::Stats gVoiceAudioProcessor::getStats() const {
	return {
		state->capturedframes.load(std::memory_order_relaxed),
		state->captureoverruns.load(std::memory_order_relaxed),
		state->encodedpackets.load(std::memory_order_relaxed),
		state->encodeerrors.load(std::memory_order_relaxed),
		state->outgoingqueuedrops.load(std::memory_order_relaxed),
		state->receivedpackets.load(std::memory_order_relaxed),
		state->receivedbytes.load(std::memory_order_relaxed),
		state->incomingqueuedrops.load(std::memory_order_relaxed),
		state->duplicatepackets.load(std::memory_order_relaxed),
		state->latepackets.load(std::memory_order_relaxed),
		state->reorderedpackets.load(std::memory_order_relaxed),
		state->missingpackets.load(std::memory_order_relaxed),
		state->plcframes.load(std::memory_order_relaxed),
		state->decodedpackets.load(std::memory_order_relaxed),
		state->decodeerrors.load(std::memory_order_relaxed),
		state->mixedframes.load(std::memory_order_relaxed),
		state->playbackoverruns.load(std::memory_order_relaxed),
		state->playbackunderruns.load(std::memory_order_relaxed),
		state->jitterdepth.load(std::memory_order_relaxed),
		state->activespeakers.load(std::memory_order_relaxed)
	};
}

std::vector<gVoiceAudioProcessor::SpeakerStats> gVoiceAudioProcessor::getSpeakerStats() const {
	std::lock_guard<std::mutex> lock(state->snapshotmutex);
	return state->speakersnapshot;
}

std::string gVoiceAudioProcessor::getLastError() const {
	return state->getError();
}

void gVoiceAudioProcessor::mixFrames(const std::vector<const std::int16_t*>& inputs, std::size_t samples,
		std::int16_t* output) {
	if (!output) return;
	if (inputs.empty()) {
		std::fill(output, output + samples, 0);
		return;
	}
	for (std::size_t i = 0; i < samples; i++) {
		std::int32_t sum = 0;
		for (const std::int16_t* input : inputs) {
			if (input) sum += input[i];
		}
		std::int32_t normalized = sum / static_cast<std::int32_t>(inputs.size());
		normalized = std::max<std::int32_t>(std::numeric_limits<std::int16_t>::min(),
				std::min<std::int32_t>(std::numeric_limits<std::int16_t>::max(), normalized));
		output[i] = static_cast<std::int16_t>(normalized);
	}
}
