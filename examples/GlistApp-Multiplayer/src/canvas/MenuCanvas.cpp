#include "MenuCanvas.h"
#include "canvas/GameCanvas.h"
#include "net/GameBackendLocal.h"
#include "net/GameBackendRemote.h"

MenuCanvas::MenuCanvas(gApp* root)
	: gBaseCanvas(root), root(root),
	  state(STATE_MODE_SELECT), ishost(false), portinput("25000") {
}

MenuCanvas::~MenuCanvas() {
}

void MenuCanvas::setup() {
	logo.loadImage("glistengine_logo.png");
	font.loadFont("FreeSansBold.ttf", 18);
	fontsmall.loadFont("FreeSans.ttf", 14);
}

void MenuCanvas::update() {
}

void MenuCanvas::draw() {
	logo.draw((getWidth() - logo.getWidth()) / 2, (getHeight() - logo.getHeight()) / 2);

	int cx = getWidth() / 2;
	int y = 80;

	setColor(255, 255, 255);
	std::string title = "Multiplayer Setup";
	font.drawText(title, cx - font.getStringWidth(title) / 2, y);
	y += 50;

	if (state == STATE_MODE_SELECT) {
		setColor(200, 200, 200);
		std::string line1 = "Press [1] to Host a server";
		std::string line2 = "Press [2] to Join as client";
		fontsmall.drawText(line1, cx - fontsmall.getStringWidth(line1) / 2, y);
		y += 30;
		fontsmall.drawText(line2, cx - fontsmall.getStringWidth(line2) / 2, y);
	} else if (state == STATE_IP_INPUT) {
		setColor(200, 200, 200);
		std::string label = ishost ? "Bind IP (Enter for 0.0.0.0):" : "Server IP:";
		fontsmall.drawText(label, cx - fontsmall.getStringWidth(label) / 2, y);
		y += 30;

		setColor(0, 255, 100);
		std::string display = "> " + ipinput + "_";
		fontsmall.drawText(display, cx - fontsmall.getStringWidth(display) / 2, y);
		y += 40;

		setColor(150, 150, 150);
		std::string hint = "Type IP and press Enter";
		fontsmall.drawText(hint, cx - fontsmall.getStringWidth(hint) / 2, y);
	} else if (state == STATE_PORT_INPUT) {
		setColor(200, 200, 200);
		std::string label = "Port (Enter for 25000):";
		fontsmall.drawText(label, cx - fontsmall.getStringWidth(label) / 2, y);
		y += 30;

		setColor(0, 255, 100);
		std::string display = "> " + portinput + "_";
		fontsmall.drawText(display, cx - fontsmall.getStringWidth(display) / 2, y);
		y += 40;

		setColor(150, 150, 150);
		std::string hint = "Type port and press Enter";
		fontsmall.drawText(hint, cx - fontsmall.getStringWidth(hint) / 2, y);
	}

	setColor(255, 255, 255);
}

void MenuCanvas::keyPressed(int key) {
	if (state == STATE_MODE_SELECT) {
		if (key == G_KEY_1) {
			ishost = true;
			state = STATE_IP_INPUT;
			ipinput = "0.0.0.0";
		} else if (key == G_KEY_2) {
			ishost = false;
			state = STATE_IP_INPUT;
			ipinput = "";
		}
	} else if (state == STATE_IP_INPUT) {
		if (key == G_KEY_ENTER) {
			if (ipinput.empty()) {
				ipinput = ishost ? "0.0.0.0" : "127.0.0.1";
			}
			state = STATE_PORT_INPUT;
		} else if (key == G_KEY_BACKSPACE) {
			if (!ipinput.empty()) ipinput.pop_back();
		}
	} else if (state == STATE_PORT_INPUT) {
		if (key == G_KEY_ENTER) {
			if (portinput.empty()) portinput = "25000";
			uint16_t port = (uint16_t)std::stoi(portinput);

			std::unique_ptr<GameBackend> backend;
			if (ishost) {
				backend = std::make_unique<GameBackendLocal>(ipinput, port);
			} else {
				backend = std::make_unique<GameBackendRemote>(ipinput, port);
			}

			backend->start();

			GameCanvas* gameCanvas = new GameCanvas(root, std::move(backend));
			root->setCurrentCanvas(gameCanvas);
		} else if (key == G_KEY_BACKSPACE) {
			if (!portinput.empty()) portinput.pop_back();
		}
	}
}

void MenuCanvas::keyReleased(int key) {
}

void MenuCanvas::charPressed(unsigned int codepoint) {
	if (state == STATE_IP_INPUT) {
		char c = (char)codepoint;
		if ((c >= '0' && c <= '9') || c == '.') {
			ipinput += c;
		}
	} else if (state == STATE_PORT_INPUT) {
		char c = (char)codepoint;
		if (c >= '0' && c <= '9') {
			portinput += c;
		}
	}
}

void MenuCanvas::mouseMoved(int x, int y) {
}

void MenuCanvas::mouseDragged(int x, int y, int button) {
}

void MenuCanvas::mousePressed(int x, int y, int button) {
}

void MenuCanvas::mouseReleased(int x, int y, int button) {
}

void MenuCanvas::mouseScrolled(int x, int y) {
}

void MenuCanvas::mouseEntered() {
}

void MenuCanvas::mouseExited() {
}

void MenuCanvas::windowResized(int w, int h) {
}

void MenuCanvas::showNotify() {
}

void MenuCanvas::hideNotify() {
}
