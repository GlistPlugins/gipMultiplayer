/*
 * GameCanvas.h
 *
 * The main game screen. Owns the local player (a gBox), handles WASD+QE
 * input for movement, and uses the GameBackend for network syncing.
 *
 * On setup:
 *   - Attaches the local gBox as a local node (position is sent to peers).
 *   - Registers onJoin/onLeave callbacks to create/destroy remote visuals.
 *
 * On update:
 *   - Moves the local box based on key state.
 *   - Calls backend->update() which syncs positions in both directions.
 *
 * On draw:
 *   - Draws the local box (green) and all remote boxes (red).
 */

#pragma once

#include "gBaseCanvas.h"
#include "gApp.h"
#include "gImage.h"
#include "gFont.h"
#include "gBox.h"
#include "gRectangle.h"
#include "net/GameBackend.h"

class GameCanvas : public gBaseCanvas {
public:
	GameCanvas(gApp* root, std::unique_ptr<GameBackend> backend);
	virtual ~GameCanvas();

	void setup();
	void update();
	void draw();

	void keyPressed(int key);
	void keyReleased(int key);
	void charPressed(unsigned int codepoint);
	void mouseMoved(int x, int y);
	void mouseDragged(int x, int y, int button);
	void mousePressed(int x, int y, int button);
	void mouseReleased(int x, int y, int button);
	void mouseScrolled(int x, int y);
	void mouseEntered();
	void mouseExited();
	void windowResized(int w, int h);
	void showNotify();
	void hideNotify();

private:
	gApp* root;
	std::unique_ptr<GameBackend> backend;

	gImage logo;
	gFont font;
	gRectangle boardrect;

	uint32_t myid;
	gBox localbox;
	bool wkey{}, skey{}, akey{}, dkey{}, qkey{}, ekey{};

	// Remote players created dynamically via onJoin callback.
	// In a real game, this could be gModel, gModelGameObject, etc.
	// As long as it's extending from gNode, it would be fine
	std::unordered_map<uint32_t, std::unique_ptr<gBox>> remoteboxes;
};
