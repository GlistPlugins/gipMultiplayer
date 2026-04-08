#include "GameCanvas.h"
#include <random>
#include <chrono>

static uint32_t MakeRandomId() {
	std::mt19937 ridg((uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
	return ridg();
}

GameCanvas::GameCanvas(gApp* root, std::unique_ptr<GameBackend> backend)
	: gBaseCanvas(root), root(root), backend(std::move(backend)), myid(MakeRandomId()) {
}

GameCanvas::~GameCanvas() {
}

void GameCanvas::setup() {
	logo.loadImage("glistengine_logo.png");
	font.loadFont("FreeSansBold.ttf", 10);

	// Set up local player box and register it as a local node
	localbox.setScale(0.1f);
	localbox.setPosition(0.0f, -0.2f, -0.5f);
	backend->attachNode(myid, &localbox, /*local=*/true); // true means that we send the position/orientation and others would update (we are the authority)

	// When a new remote node appears, create a visual for it.
	// In a real game you'd create a gModel, load a mesh, etc.
	backend->setOnJoin([this](uint32_t id) {
		auto box = std::make_unique<gBox>();
		box->setScale(0.1f);
		backend->attachNode(id, box.get(), /*local=*/false); // false means that we receive the position/orientation and don't send.
		remoteboxes[id] = std::move(box);
	});

	// When a remote node disconnects, clean up
	backend->setOnLeave([this](uint32_t id) {
		backend->detachNode(id);
		remoteboxes.erase(id);
	});
}

void GameCanvas::update() {
	// Move local box based on key state
	// This could be a physics object, or player.
	// Doesn't really matter, real magic happens in GameBackend::update
	// Which would read the position from the attached local nodes (see setup for attachNode)
	// and send them to remotes.
	float speed = 1.0f * root->getElapsedTime();
	float dx = 0, dy = 0, dz = 0;
	if (wkey) dz -= speed;
	if (skey) dz += speed;
	if (akey) dx -= speed;
	if (dkey) dx += speed;
	if (qkey) dy += speed;
	if (ekey) dy -= speed;
	localbox.move(dx, dy, dz);

	// Process incoming network events and send local node positions
	// For nodes that are owned by the remote, we set the received position
	// For nodes that are owned by us, we send the position to remotes
	backend->update();
}

void GameCanvas::draw() {
	logo.draw((getWidth() - logo.getWidth()) / 2, (getHeight() - logo.getHeight()) / 2);

	enableAlphaBlending();
	setColor(255, 255, 255, 100);
	boardrect.draw(0, 0, 120, 200, true);
	disableAlphaBlending();

	setColor(255, 255, 255);
	font.drawText("Nodes:", 5, 5 + font.getStringHeight("Nodes:"));

	// Draw local nodes in green
	setColor(0, 255, 0);
	font.drawText(std::to_string(myid), 5, 20 + font.getStringHeight(std::to_string(myid)));
	localbox.draw();

	// Draw remote nodes in red
	setColor(255, 0, 0);
	int index = 2;
	for (auto& kv : remoteboxes) {
		kv.second->draw();
		font.drawText(std::to_string(kv.first), 5, index * (15 + font.getStringHeight(std::to_string(kv.first))));
		index++;
	}

	setColor(255, 255, 255);
}

void GameCanvas::keyPressed(int key) {
	if (key == G_KEY_W) wkey = true;
	if (key == G_KEY_S) skey = true;
	if (key == G_KEY_A) akey = true;
	if (key == G_KEY_D) dkey = true;
	if (key == G_KEY_Q) qkey = true;
	if (key == G_KEY_E) ekey = true;
}

void GameCanvas::keyReleased(int key) {
	if (key == G_KEY_W) wkey = false;
	if (key == G_KEY_S) skey = false;
	if (key == G_KEY_A) akey = false;
	if (key == G_KEY_D) dkey = false;
	if (key == G_KEY_Q) qkey = false;
	if (key == G_KEY_E) ekey = false;
}

void GameCanvas::charPressed(unsigned int codepoint) {
}

void GameCanvas::mouseMoved(int x, int y) {
}

void GameCanvas::mouseDragged(int x, int y, int button) {
}

void GameCanvas::mousePressed(int x, int y, int button) {
}

void GameCanvas::mouseReleased(int x, int y, int button) {
}

void GameCanvas::mouseScrolled(int x, int y) {
}

void GameCanvas::mouseEntered() {
}

void GameCanvas::mouseExited() {
}

void GameCanvas::windowResized(int w, int h) {
}

void GameCanvas::showNotify() {
}

void GameCanvas::hideNotify() {
}
