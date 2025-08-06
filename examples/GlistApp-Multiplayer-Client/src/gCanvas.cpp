/*
* gCanvas.cpp
*
*  Created on: May 6, 2020
*      Author: noyan
*/


#include "gCanvas.h"
#include <random>
#include <chrono>

static uint32_t MakeRandomId() {
	std::mt19937 ridg((uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
	return ridg();
}

static constexpr bool isHost = false; //Set this variable to 'true' for a host, and to 'false' for a client.
static const char* bindIP = "0.0.0.0";
static const char* serverIP = "/*YOUR_IP_HERE*/"; //Enter your private IP here.
static constexpr uint16_t port = 25000;

gCanvas::gCanvas(gApp* root) : gBaseCanvas(root) {
	this->root = root;
}

gCanvas::~gCanvas() {
}

void gCanvas::setup() {
	logo.loadImage("glistengine_logo.png");
	font.loadFont("FreeSansBold.ttf", 10);
	if (isHost) server = createGameServer(bindIP, port);
	client = createGameClient(serverIP, port);
	player = new Player(getWidth() * 0.0f, -0.2f, -0.5f);
	myId = MakeRandomId();

	setRemoteStateCallback([this](uint32_t id, float x, float y, float z){
		std::lock_guard<std::mutex> lock(queueMutex);
		queue.push_back(RemoteState{id, x, y, z});
	});

	setPlayerDisconnectCallback([this](uint32_t id) {
		std::lock_guard<std::mutex> lk(queueMutex);
		queue.push_back(RemoteState{id, -999, -999, -999});
	});
}

void gCanvas::update() {
	std::vector<RemoteState> batch;
	std::lock_guard<std::mutex> lock(queueMutex);
	batch.swap(queue);

	for (auto& remoteState : batch) {
		if (remoteState.id == myId) continue;

		if (remoteState.x == -999) {
			remotes.erase(remoteState.id);
			continue;
		}

		auto it = remotes.find(remoteState.id);

		if (it == remotes.end()) {
			auto rp = std::make_unique<Player>(remoteState.x, remoteState.y, remoteState.z);
			remotes.emplace(remoteState.id, std::move(rp));
		} else {
			it->second->setCords(remoteState.x, remoteState.y, remoteState.z);
		}
	}

	player->Update(1.0f * root->getElapsedTime());
	sendLocalState(myId, player->getX(), player->getY(), player->getZ());
}

void gCanvas::draw() {
	logo.draw((getWidth() - logo.getWidth()) / 2, (getHeight() - logo.getHeight()) / 2);
	enableAlphaBlending();
	setColor(255, 255, 255, 100);
	boardRect.draw(0, 0, 100, 200, true);
	disableAlphaBlending();
	setColor(255, 255, 255);
	font.drawText("Players:", 5, 5 + font.getStringHeight("Players:"));
	setColor(0, 255, 0);
	font.drawText(std::to_string(myId), 5, 20 + font.getStringHeight(std::to_string(myId)));
	player->Draw();
	setColor(255, 0, 0);
	int index = 2;
	for (auto& kv : remotes) {
		kv.second->Draw();
		if (kv.first) {
			font.drawText(std::to_string(kv.first), 5, index * (15 + font.getStringHeight(std::to_string(kv.first))));
		}
		index++;
	}
	setColor(255, 255, 255);
}

void gCanvas::keyPressed(int key) {
//	gLogi("gCanvas") << "keyPressed:" << key;
	player->KeyPressed(key);
}

void gCanvas::keyReleased(int key) {
//	gLogi("gCanvas") << "keyReleased:" << key;
	player->KeyReleased(key);
}

void gCanvas::charPressed(unsigned int codepoint) {
//	gLogi("gCanvas") << "charPressed:" << gCodepointToStr(codepoint);
}

void gCanvas::mouseMoved(int x, int y) {
//	gLogi("gCanvas") << "mouseMoved" << ", x:" << x << ", y:" << y;
}

void gCanvas::mouseDragged(int x, int y, int button) {
//	gLogi("gCanvas") << "mouseDragged" << ", x:" << x << ", y:" << y << ", b:" << button;
}

void gCanvas::mousePressed(int x, int y, int button) {
//	gLogi("gCanvas") << "mousePressed" << ", x:" << x << ", y:" << y << ", b:" << button;
}

void gCanvas::mouseReleased(int x, int y, int button) {
//	gLogi("gCanvas") << "mouseReleased" << ", button:" << button;
}

void gCanvas::mouseScrolled(int x, int y) {
//	gLogi("gCanvas") << "mouseScrolled" << ", x:" << x << ", y:" << y;
}

void gCanvas::mouseEntered() {

}

void gCanvas::mouseExited() {

}

void gCanvas::windowResized(int w, int h) {

}

void gCanvas::showNotify() {

}

void gCanvas::hideNotify() {

}

