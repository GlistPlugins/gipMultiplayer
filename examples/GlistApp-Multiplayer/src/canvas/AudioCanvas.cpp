/*
 * AudioCanvas.cpp
 */

#include "AudioCanvas.h"

#include <algorithm>
#include <iomanip>
#include <sstream>


AudioCanvas::AudioCanvas(gApp* root)
		: gBaseCanvas(root), root(root), selectedmode(gVoiceLoopback::MODE_RAW_PCM), codecselftestpassed(false) {
}

AudioCanvas::~AudioCanvas() {
	voiceloopback.stop();
}

void AudioCanvas::setup() {
	logo.loadImage("glistengine_logo.png");
	titlefont.loadFont("FreeSansBold.ttf", 28);
	bodyfont.loadFont("FreeSans.ttf", 18);
	codecselftestpassed = voiceloopback.runCodecSelfTest(codecselftestresult);
	statusmessage = "Microphone is stopped. Use headphones, then press [1] or [2].";
}

void AudioCanvas::update() {
}

void AudioCanvas::draw() {
	int left = std::max(40, getWidth() / 2 - 430);
	int y = 80;
	setColor(255, 255, 255);
	titlefont.drawText("gipMultiplayer Voice Pipeline", left, y);
	y += 50;
	setColor(185, 195, 210);
	bodyfont.drawText("48 kHz mono / 20 ms Opus frames / 2000 ms software delay", left, y);
	y += 48;
	setColor(245, 195, 80);
	bodyfont.drawText("Use headphones to prevent microphone feedback.", left, y);
	y += 55;
	setColor(225, 230, 238);
	bodyfont.drawText("[1] Start raw PCM loopback", left, y);
	y += 32;
	bodyfont.drawText("[2] Start Opus encode/decode loopback", left, y);
	y += 32;
	bodyfont.drawText("[Space] Stop or restart the selected mode", left, y);
	y += 55;
	setColor(codecselftestpassed ? 100 : 245, codecselftestpassed ? 220 : 100, codecselftestpassed ? 145 : 100);
	bodyfont.drawText("Codec self-test: " + codecselftestresult, left, y);
	y += 38;
	setColor(voiceloopback.isRunning() ? 100 : 190, voiceloopback.isRunning() ? 220 : 195,
			voiceloopback.isRunning() ? 145 : 205);
	bodyfont.drawText(std::string("State: ") + (voiceloopback.isRunning() ? "RUNNING" : "STOPPED") +
			" / selected mode: " + gVoiceLoopback::getModeName(selectedmode), left, y);
	y += 32;
	setColor(210, 215, 225);
	bodyfont.drawText(statusmessage, left, y);
	if (voiceloopback.isRunning()) {
		gVoiceLoopback::Stats stats = voiceloopback.getStats();
		std::ostringstream stream;
		stream << std::fixed << std::setprecision(1);
		y += 48;
		stream << "PCM processed: " << stats.pcmbytes / 1024.0 << " KiB";
		if (selectedmode == gVoiceLoopback::MODE_OPUS) {
			stream << " / Opus: " << stats.opusbytes / 1024.0 << " KiB";
			if (stats.opusbytes > 0) {
				stream << " / ratio: " << static_cast<double>(stats.pcmbytes) / stats.opusbytes << ":1";
			}
		}
		bodyfont.drawText(stream.str(), left, y);
		stream.str("");
		stream.clear();
		y += 32;
		stream << "Packets encoded/decoded: " << stats.encodedpackets << "/" << stats.decodedpackets;
		bodyfont.drawText(stream.str(), left, y);
		stream.str("");
		stream.clear();
		y += 32;
		stream << "Capture/playback overruns: " << stats.captureoverruns << "/" << stats.playbackoverruns
				<< " / playback underruns: " << stats.playbackunderruns << " / codec errors: " << stats.codecerrors;
		setColor(stats.captureoverruns == 0 && stats.playbackoverruns == 0 && stats.codecerrors == 0 ? 150 : 245,
				stats.captureoverruns == 0 && stats.playbackoverruns == 0 && stats.codecerrors == 0 ? 210 : 110, 150);
		bodyfont.drawText(stream.str(), left, y);
	}
	setColor(255, 255, 255);
	logo.draw(getWidth() - logo.getWidth() / 2 - 40, getHeight() - logo.getHeight() / 2 - 30,
			logo.getWidth() / 2, logo.getHeight() / 2);
}

void AudioCanvas::keyPressed(int key) {
	if (key == G_KEY_1 || key == G_KEY_2) {
		selectedmode = key == G_KEY_1 ? gVoiceLoopback::MODE_RAW_PCM : gVoiceLoopback::MODE_OPUS;
		if (voiceloopback.start(selectedmode)) {
			statusmessage = std::string(gVoiceLoopback::getModeName(selectedmode)) +
					" is active. Speak into the microphone.";
		} else {
			statusmessage = "Could not start audio: " + voiceloopback.getLastError();
		}
	} else if (key == G_KEY_SPACE) {
		if (voiceloopback.isRunning()) {
			voiceloopback.stop();
			statusmessage = "Microphone is stopped.";
		} else if (voiceloopback.start(selectedmode)) {
			statusmessage = std::string(gVoiceLoopback::getModeName(selectedmode)) +
					" is active. Speak into the microphone.";
		} else {
			statusmessage = "Could not start audio: " + voiceloopback.getLastError();
		}
	}
}

void AudioCanvas::keyReleased(int key) {
}

void AudioCanvas::charPressed(unsigned int codepoint) {
}

void AudioCanvas::mouseMoved(int x, int y) {
}

void AudioCanvas::mouseDragged(int x, int y, int button) {
}

void AudioCanvas::mousePressed(int x, int y, int button) {
}

void AudioCanvas::mouseReleased(int x, int y, int button) {
}

void AudioCanvas::mouseScrolled(int x, int y) {
}

void AudioCanvas::mouseEntered() {
}

void AudioCanvas::mouseExited() {
}

void AudioCanvas::windowResized(int w, int h) {
}

void AudioCanvas::showNotify() {
}

void AudioCanvas::hideNotify() {
}
