/*
 * AudioCanvas.h
 *
 * Local microphone and Opus loopback diagnostics.
 */

#pragma once

#include "gBaseCanvas.h"
#include "gApp.h"
#include "audio/gVoiceLoopback.h"
#include "gFont.h"
#include "gImage.h"


class AudioCanvas : public gBaseCanvas {
public:
	AudioCanvas(gApp* root);
	virtual ~AudioCanvas();

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
	gImage logo;
	gFont titlefont;
	gFont bodyfont;
	gVoiceLoopback voiceloopback;
	gVoiceLoopback::Mode selectedmode;
	bool codecselftestpassed;
	std::string codecselftestresult;
	std::string statusmessage;
};
