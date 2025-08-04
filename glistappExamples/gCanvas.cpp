#include "gCanvas.h"
#include "Client.h"
#include "Server.h"
#include <random>
#include <chrono>

static uint32_t MakeRandomId() {
	std::mt19937 ridg((uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
	return ridg();
}

static constexpr bool isHost = true;
static const char* bindIP = "0.0.0.0";
static const char* serverIP = "YOUR_IP_HERE";
static constexpr uint16_t port = 25000;

gCanvas::gCanvas(gApp* root) : gBaseCanvas(root), root(root) {}
gCanvas::~gCanvas() {}

void gCanvas::setup() {
	if (isHost) server = createGameServer(bindIP, port);

	client = createGameClient(isHost ? "YOUR_IP_HERE" : serverIP, port);
	player = new Player(getWidth() / 2, getHeight() / 2, 100, 100);
	myId = MakeRandomId();

	setRemoteStateCallback([this](uint32_t id, float x, float y, bool green){
		std::lock_guard<std::mutex> lock(queueMutex);
		queue.push_back(RemoteState{id, x, y, green});
	});
}

void gCanvas::update() {
	std::vector<RemoteState> batch;
	{
		std::lock_guard<std::mutex> lock(queueMutex);
		batch.swap(queue);
	}

	for (auto& remoteState : batch) {
		if (remoteState.id == myId) continue;

		auto it = remotes.find(remoteState.id);

		if (it == remotes.end()) {
			auto rp = std::make_unique<Player>(remoteState.x, remoteState.y, 100, 100);
			rp->setGreen(remoteState.green);
			remotes.emplace(remoteState.id, std::move(rp));
		} else {
			it->second->setCords(remoteState.x, remoteState.y);
			it->second->setGreen(remoteState.green);
		}
	}

	player->Update();
	sendLocalState(myId, player->getX(), player->getY(), player->getGreen());
}

void gCanvas::draw() {
	player->Draw();
	for (auto& kv : remotes) kv.second->Draw();
}

void gCanvas::keyPressed(int key) { player->KeyPressed(key); }
void gCanvas::keyReleased(int key) { player->KeyReleased(key); }
void gCanvas::charPressed(unsigned int) {}
void gCanvas::mouseMoved(int, int) {}
void gCanvas::mouseDragged(int, int, int) {}
void gCanvas::mousePressed(int, int, int) {}
void gCanvas::mouseReleased(int, int, int) {}
void gCanvas::mouseScrolled(int, int) {}
void gCanvas::mouseEntered() {}
void gCanvas::mouseExited() {}
void gCanvas::windowResized(int, int) {}
void gCanvas::showNotify() {}
void gCanvas::hideNotify() {}