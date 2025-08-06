 /*
 * gCanvas.h
 *
 *  Created on: May 6, 2020
 *      Author: noyan
 */

#ifndef GCANVAS_H_
#define GCANVAS_H_

#include "gBaseCanvas.h"
#include "gApp.h"
#include "gImage.h"
#include "gRectangle.h"
#include "gipServerHandler.h"
#include "gipClientHandler.h"
#include "Player.h"

struct RemoteState {
	uint32_t id;
	float x;
	float y;
	float z;
};

class gCanvas : public gBaseCanvas {
public:
	gCanvas(gApp* root);
	virtual ~gCanvas();

	void setup();
	void update();
	void draw();

	void keyPressed(int key);
	void keyReleased(int key);
	void charPressed(unsigned int codepoint);
	void mouseMoved(int x, int y );
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
	gImage logo;
	std::unique_ptr<znet::Server> server;
	std::unique_ptr<znet::Client> client;
	Player* player;
	uint32_t myId = 0;
	std::unordered_map<uint32_t, std::unique_ptr<Player>> remotes;
	std::mutex queueMutex;
	std::vector<RemoteState> queue;
	gFont font;
	gRectangle boardRect;
};

#endif /* GCANVAS_H_ */
