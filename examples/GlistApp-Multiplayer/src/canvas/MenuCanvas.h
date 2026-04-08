/*
 * MenuCanvas.h
 *
 * Startup screen that lets the user choose Host or Client mode,
 * enter an IP address and port, then creates the appropriate
 * GameBackend and switches to GameCanvas.
 */

#pragma once

#include "gBaseCanvas.h"
#include "gApp.h"
#include "gImage.h"
#include "gFont.h"

class MenuCanvas : public gBaseCanvas {
public:
	MenuCanvas(gApp* root);
	virtual ~MenuCanvas();

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
	enum State {
		STATE_MODE_SELECT,
		STATE_IP_INPUT,
		STATE_PORT_INPUT,
	};

	gApp* root;
	gImage logo;
	gFont font;
	gFont fontsmall;

	State state;
	bool ishost;
	std::string ipinput;
	std::string portinput;
};
