#include "gCanvas.h"

#include <algorithm>


gCanvas::gCanvas(gApp* app) : gBaseCanvas(app), app(app) {
}

gCanvas::~gCanvas() {
	shutdown();
}

void gCanvas::setup() {
	titlefont.loadFont("FreeSansBold.ttf", 28);
	bodyfont.loadFont("FreeSans.ttf", 18);
	smallfont.loadFont("FreeSans.ttf", 14);
	codecselftestpassed = loopback.runCodecSelfTest(codecselftest);
}

void gCanvas::update() {
	if (screen == Screen::VOICE) {
		voiceserver.update();
		voiceclient.update();
	}
}

void gCanvas::draw() {
	setColor(20, 24, 33);
	background.draw(0, 0, getWidth(), getHeight(), true);
	setColor(240, 243, 248);
	titlefont.drawText("gipMultiplayer Team Voice", 60, 70);
	if (screen == Screen::MENU) drawMenu();
	if (screen == Screen::IP_INPUT || screen == Screen::PORT_INPUT) drawInput();
	if (screen == Screen::VOICE) drawVoice();
	if (screen == Screen::LOOPBACK) drawLoopback();
}

void gCanvas::drawMenu() {
	setColor(185, 195, 210);
	bodyfont.drawText("Clean single-gCanvas example using encrypted ZDT and Opus", 60, 115);
	setColor(240, 243, 248);
	bodyfont.drawText("[1] Host team voice", 60, 180);
	bodyfont.drawText("[2] Join team voice", 60, 220);
	bodyfont.drawText("[3] Local Opus microphone loopback", 60, 260);
	setColor(codecselftestpassed ? 105 : 245, codecselftestpassed ? 220 : 110, 145);
	smallfont.drawText("Codec self-test: " + codecselftest, 60, 325);
	setColor(245, 195, 80);
	smallfont.drawText("Use headphones to prevent acoustic feedback.", 60, 365);
}

void gCanvas::drawInput() {
	setColor(185, 195, 210);
	if (screen == Screen::IP_INPUT) {
		bodyfont.drawText(connectionmode == ConnectionMode::HOST ?
				"Bind IP (0.0.0.0 accepts LAN connections):" : "Server IP:", 60, 165);
		setColor(100, 220, 145);
		bodyfont.drawText("> " + ipinput + "_", 60, 210);
	} else {
		bodyfont.drawText("UDP port:", 60, 165);
		setColor(100, 220, 145);
		bodyfont.drawText("> " + portinput + "_", 60, 210);
	}
	setColor(185, 195, 210);
	smallfont.drawText("Enter: continue   Backspace: delete   Esc: menu", 60, 265);
	if (!message.empty()) {
		setColor(245, 110, 120);
		smallfont.drawText(message, 60, 305);
	}
}

void gCanvas::drawVoice() {
	auto clientstatus = voiceclient.getStatus();
	auto serverstatus = voiceserver.getStatus();
	std::string state;
	if (!clientstatus.initialized) {
		state = "AUDIO ERROR";
	} else if (!clientstatus.connected) {
		state = clientstatus.error.empty() ? "CONNECTING" : "NETWORK ERROR";
	} else if (!clientstatus.authorized) {
		state = "WAITING FOR SERVER";
	} else if (clientstatus.transmitting) {
		state = "TRANSMITTING";
	} else {
		state = "LISTENING";
	}
	setColor(clientstatus.authorized ? 105 : 245, clientstatus.authorized ? 220 : 140, 130);
	bodyfont.drawText("Voice: " + state, 60, 155);
	setColor(240, 243, 248);
	bodyfont.drawText("Hold [V] to talk. Receiving is always active.", 60, 205);
	smallfont.drawText("Sent packets: " + std::to_string(clientstatus.stats.sentpackets), 60, 255);
	smallfont.drawText("Received packets: " + std::to_string(clientstatus.stats.receivedpackets), 60, 285);
	smallfont.drawText("Decoded packets: " + std::to_string(clientstatus.stats.decodedpackets), 60, 315);
	smallfont.drawText("Active speakers: " + std::to_string(clientstatus.stats.activespeakers), 60, 345);
	if (connectionmode == ConnectionMode::HOST) {
		smallfont.drawText("Server peers/relayed: " + std::to_string(serverstatus.stats.activeconnections) + "/" +
				std::to_string(serverstatus.stats.relayedpackets), 60, 385);
	}
	if (!clientstatus.error.empty()) {
		setColor(245, 110, 120);
		smallfont.drawText(clientstatus.error, 60, 430);
	} else if (!serverstatus.error.empty()) {
		setColor(245, 110, 120);
		smallfont.drawText(serverstatus.error, 60, 430);
	}
	setColor(185, 195, 210);
	smallfont.drawText("[Esc] Disconnect and return to menu", 60, 485);
}

void gCanvas::drawLoopback() {
	auto stats = loopback.getStats();
	setColor(loopback.isRunning() ? 105 : 245, loopback.isRunning() ? 220 : 140, 130);
	bodyfont.drawText(loopback.isRunning() ? "Local Opus loopback: RUNNING" : "Local Opus loopback: STOPPED", 60, 155);
	setColor(240, 243, 248);
	bodyfont.drawText("Speak into the microphone; playback is delayed by two seconds.", 60, 205);
	smallfont.drawText("Encoded/decoded: " + std::to_string(stats.encodedpackets) + "/" +
			std::to_string(stats.decodedpackets), 60, 255);
	smallfont.drawText("Capture/playback overruns: " + std::to_string(stats.captureoverruns) + "/" +
			std::to_string(stats.playbackoverruns), 60, 285);
	smallfont.drawText("Codec errors: " + std::to_string(stats.codecerrors), 60, 315);
	if (!message.empty()) {
		setColor(245, 110, 120);
		smallfont.drawText(message, 60, 360);
	}
	setColor(185, 195, 210);
	smallfont.drawText("[Space] Stop/restart   [Esc] Menu", 60, 410);
}

void gCanvas::keyPressed(int key) {
	if (screen == Screen::MENU) {
		if (key == G_KEY_1 || key == G_KEY_2) {
			connectionmode = key == G_KEY_1 ? ConnectionMode::HOST : ConnectionMode::CLIENT;
			ipinput = connectionmode == ConnectionMode::HOST ? "0.0.0.0" : "";
			portinput = "25000";
			message.clear();
			screen = Screen::IP_INPUT;
		} else if (key == G_KEY_3) {
			message.clear();
			if (!loopback.start(gVoiceLoopback::MODE_OPUS)) message = loopback.getLastError();
			screen = Screen::LOOPBACK;
		}
		return;
	}

	if (screen == Screen::IP_INPUT) {
		if (key == G_KEY_ENTER) {
			if (ipinput.empty()) ipinput = connectionmode == ConnectionMode::HOST ? "0.0.0.0" : "127.0.0.1";
			screen = Screen::PORT_INPUT;
		} else if (key == G_KEY_BACKSPACE && !ipinput.empty()) {
			ipinput.pop_back();
		} else if (key == G_KEY_ESC) {
			returnToMenu();
		}
		return;
	}

	if (screen == Screen::PORT_INPUT) {
		if (key == G_KEY_ENTER) {
			startVoice();
		} else if (key == G_KEY_BACKSPACE && !portinput.empty()) {
			portinput.pop_back();
		} else if (key == G_KEY_ESC) {
			returnToMenu();
		}
		return;
	}

	if (screen == Screen::VOICE) {
		if (key == G_KEY_V && !voicepressed) {
			voicepressed = true;
			voiceclient.startTransmitting();
		} else if (key == G_KEY_ESC) {
			returnToMenu();
		}
		return;
	}

	if (screen == Screen::LOOPBACK) {
		if (key == G_KEY_SPACE) {
			if (loopback.isRunning()) {
				loopback.stop();
			} else if (!loopback.start(gVoiceLoopback::MODE_OPUS)) {
				message = loopback.getLastError();
			}
		} else if (key == G_KEY_ESC) {
			returnToMenu();
		}
	}
}

void gCanvas::keyReleased(int key) {
	if (key == G_KEY_V && voicepressed) {
		voicepressed = false;
		voiceclient.stopTransmitting();
	}
}

void gCanvas::charPressed(unsigned int codepoint) {
	char character = static_cast<char>(codepoint);
	if (screen == Screen::IP_INPUT && ((character >= '0' && character <= '9') || character == '.')) {
		ipinput += character;
	} else if (screen == Screen::PORT_INPUT && character >= '0' && character <= '9') {
		portinput += character;
	}
}

void gCanvas::startVoice() {
	std::uint16_t port = 0;
	if (!parsePort(port)) {
		message = "Port must be between 1 and 65535";
		return;
	}
	message.clear();
	shutdowncomplete = false;
	if (connectionmode == ConnectionMode::HOST) {
		if (!voiceserver.start(ipinput, port)) {
			message = voiceserver.getStatus().error;
			screen = Screen::VOICE;
			return;
		}
		std::string localaddress = ipinput == "0.0.0.0" ? "127.0.0.1" : ipinput;
		voiceclient.start(localaddress, port);
	} else {
		voiceclient.start(ipinput, port);
	}
	screen = Screen::VOICE;
}

void gCanvas::returnToMenu() {
	voicepressed = false;
	loopback.stop();
	voiceclient.shutdown();
	voiceserver.shutdown();
	message.clear();
	screen = Screen::MENU;
}

bool gCanvas::parsePort(std::uint16_t& result) const {
	if (portinput.empty()) return false;
	try {
		int value = std::stoi(portinput);
		if (value < 1 || value > 65535) return false;
		result = static_cast<std::uint16_t>(value);
		return true;
	} catch (...) {
		return false;
	}
}

void gCanvas::shutdown() {
	if (shutdowncomplete) return;
	shutdowncomplete = true;
	voicepressed = false;
	loopback.stop();
	voiceclient.shutdown();
	voiceserver.shutdown();
}

void gCanvas::mouseMoved(int, int) {
}

void gCanvas::mouseDragged(int, int, int) {
}

void gCanvas::mousePressed(int, int, int) {
}

void gCanvas::mouseReleased(int, int, int) {
}

void gCanvas::mouseScrolled(int, int) {
}

void gCanvas::mouseEntered() {
}

void gCanvas::mouseExited() {
}

void gCanvas::windowResized(int, int) {
}

void gCanvas::showNotify() {
}

void gCanvas::hideNotify() {
	voicepressed = false;
	voiceclient.stopTransmitting();
}
