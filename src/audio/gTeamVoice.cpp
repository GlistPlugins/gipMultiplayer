/*
 * gTeamVoice.cpp
 */

#include "audio/gTeamVoice.h"

#include "audio/gVoiceConstants.h"
#include "gSound.h"
#include "miniaudio.h"

#include <atomic>
#include <cstring>
#include <mutex>


class gTeamVoice::State {
public:
	State() {
		std::memset(&device, 0, sizeof(device));
	}

	~State() {
		shutdown();
	}

	bool initialize() {
		std::lock_guard<std::mutex> networklock(networkmutex);
		std::lock_guard<std::mutex> lock(lifecyclemutex);
		if (initialized.load(std::memory_order_acquire)) return true;
		lasterror.clear();
		if (deviceinitialized) {
			ma_device_uninit(&device);
			deviceinitialized = false;
			processor.stop();
		}
		if (!processor.start()) {
			lasterror = processor.getLastError();
			return false;
		}

		ma_device* enginedevice = ma_engine_get_device(gGetSoundEngine());
		ma_context* context = enginedevice == nullptr ? nullptr : ma_device_get_context(enginedevice);
		if (context == nullptr) {
			lasterror = "Could not access the GlistEngine audio context";
			processor.stop();
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
		ma_result result = ma_device_init(context, &config, &device);
		if (result != MA_SUCCESS) {
			lasterror = ma_result_description(result);
			processor.stop();
			return false;
		}
		deviceinitialized = true;
		initialized.store(true, std::memory_order_release);
		result = ma_device_start(&device);
		if (result != MA_SUCCESS) {
			lasterror = ma_result_description(result);
			initialized.store(false, std::memory_order_release);
			ma_device_uninit(&device);
			deviceinitialized = false;
			processor.stop();
			return false;
		}
		return true;
	}

	void shutdown() {
		std::lock_guard<std::mutex> networklock(networkmutex);
		std::lock_guard<std::mutex> lock(lifecyclemutex);
		initialized.store(false, std::memory_order_release);
		processor.setTransmitting(false);
		if (deviceinitialized) ma_device_stop(&device);
		if (deviceinitialized) {
			ma_device_uninit(&device);
			deviceinitialized = false;
		}
		processor.stop();
	}

	static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 framecount) {
		State* self = static_cast<State*>(device->pUserData);
		std::int16_t* outputframes = static_cast<std::int16_t*>(output);
		const std::int16_t* inputframes = static_cast<const std::int16_t*>(input);
		if (outputframes) std::memset(outputframes, 0, framecount * sizeof(std::int16_t));
		if (!self->initialized.load(std::memory_order_acquire)) return;
		if (inputframes) self->processor.pushCapturedSamples(inputframes, framecount);
		if (outputframes) self->processor.popPlaybackSamples(outputframes, framecount);
	}

	static void notificationCallback(const ma_device_notification* notification) {
		if (notification->type != ma_device_notification_type_stopped) return;
		State* self = static_cast<State*>(notification->pDevice->pUserData);
		self->initialized.store(false, std::memory_order_release);
	}

	mutable std::mutex networkmutex;
	mutable std::mutex lifecyclemutex;
	std::atomic<bool> initialized{false};
	std::atomic<bool> enabled{true};
	std::atomic<bool> localmuted{false};
	bool deviceinitialized = false;
	ma_device device;
	gVoiceAudioProcessor processor;
	std::string lasterror;
	std::atomic<std::uint64_t> sentpackets{0};
	std::atomic<std::uint64_t> sentbytes{0};
	std::atomic<std::uint64_t> sendfailures{0};
	std::atomic<std::uint64_t> rejectedpackets{0};
	std::atomic<std::uint64_t> malformedpackets{0};
};

gTeamVoice::gTeamVoice() : state(std::make_unique<State>()) {
}

gTeamVoice::~gTeamVoice() {
	shutdown();
}

bool gTeamVoice::initialize() {
	return state->initialize();
}

void gTeamVoice::shutdown() {
	state->shutdown();
}

bool gTeamVoice::isInitialized() const {
	return state->initialized.load(std::memory_order_acquire);
}

void gTeamVoice::setEnabled(bool enabled) {
	std::lock_guard<std::mutex> lock(state->networkmutex);
	state->enabled.store(enabled, std::memory_order_release);
	state->processor.setEnabled(enabled);
	if (!enabled) state->processor.setTransmitting(false);
}

bool gTeamVoice::isEnabled() const {
	return state->enabled.load(std::memory_order_acquire);
}

void gTeamVoice::startTransmitting() {
	std::lock_guard<std::mutex> lock(state->networkmutex);
	if (isEnabled()) state->processor.setTransmitting(true);
}

void gTeamVoice::stopTransmitting() {
	std::lock_guard<std::mutex> lock(state->networkmutex);
	state->processor.setTransmitting(false);
}

bool gTeamVoice::isTransmitting() const {
	return state->processor.isTransmitting();
}

void gTeamVoice::setLocalMuted(bool muted) {
	std::lock_guard<std::mutex> lock(state->networkmutex);
	state->localmuted.store(muted, std::memory_order_release);
	state->processor.setLocalMuted(muted);
}

bool gTeamVoice::isLocalMuted() const {
	return state->localmuted.load(std::memory_order_acquire);
}

bool gTeamVoice::setSpeakerMuted(std::uint64_t speakerid, bool muted) {
	return state->processor.setSpeakerMuted(speakerid, muted);
}

bool gTeamVoice::setSpeakerVolume(std::uint64_t speakerid, float volume) {
	return state->processor.setSpeakerVolume(speakerid, volume);
}

void gTeamVoice::handleSessionPacket(const gTeamVoiceSessionPacket& packet) {
	std::lock_guard<std::mutex> lock(state->networkmutex);
	if (gValidateTeamVoiceSessionPacket(packet) != gTeamVoicePacketError::NONE) {
		state->malformedpackets.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	if (!packet.enabled) {
		state->processor.setTransmitting(false);
		state->processor.clearSession();
		return;
	}
	state->processor.setSession(packet.sessionid, packet.membershipgeneration, packet.playerid);
}

void gTeamVoice::handleVoicePacket(const gTeamVoiceDownlinkPacket& packet) {
	if (!state->processor.pushIncomingPacket(packet)) {
		state->rejectedpackets.fetch_add(1, std::memory_order_relaxed);
	}
}

void gTeamVoice::reportMalformedPacket(gTeamVoicePacketError) {
	state->malformedpackets.fetch_add(1, std::memory_order_relaxed);
}

void gTeamVoice::resetSession() {
	std::lock_guard<std::mutex> lock(state->networkmutex);
	state->processor.setTransmitting(false);
	state->processor.clearSession();
}

std::size_t gTeamVoice::updateNetwork(znet::PeerSession& session) {
	std::lock_guard<std::mutex> lock(state->networkmutex);
	if (!state->initialized.load(std::memory_order_acquire) ||
			!state->enabled.load(std::memory_order_acquire) || !session.IsAlive() ||
			session.connection_type() != znet::ConnectionType::ZDT) {
		return 0;
	}
	std::size_t sent = 0;
	gTeamVoiceUplinkPacket packet;
	while (state->processor.popOutgoingPacket(packet)) {
		auto outgoing = std::make_shared<gTeamVoiceUplinkPacket>(packet);
		if (session.SendPacket(outgoing, gGetTeamVoiceDataSendOptions())) {
			state->sentpackets.fetch_add(1, std::memory_order_relaxed);
			state->sentbytes.fetch_add(packet.payload.size(), std::memory_order_relaxed);
			sent++;
		} else {
			state->sendfailures.fetch_add(1, std::memory_order_relaxed);
		}
	}
	return sent;
}

gTeamVoice::Stats gTeamVoice::getStats() const {
	gVoiceAudioProcessor::Stats audio = state->processor.getStats();
	Stats stats{};
	static_cast<gVoiceAudioProcessor::Stats&>(stats) = audio;
	stats.sentpackets = state->sentpackets.load(std::memory_order_relaxed);
	stats.sentbytes = state->sentbytes.load(std::memory_order_relaxed);
	stats.sendfailures = state->sendfailures.load(std::memory_order_relaxed);
	stats.rejectedpackets = state->rejectedpackets.load(std::memory_order_relaxed);
	stats.malformedpackets = state->malformedpackets.load(std::memory_order_relaxed);
	return stats;
}

std::vector<gVoiceAudioProcessor::SpeakerStats> gTeamVoice::getSpeakerStats() const {
	return state->processor.getSpeakerStats();
}

std::string gTeamVoice::getLastError() const {
	std::lock_guard<std::mutex> lock(state->lifecyclemutex);
	if (!state->lasterror.empty()) return state->lasterror;
	return state->processor.getLastError();
}
