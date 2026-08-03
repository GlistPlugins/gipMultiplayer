#ifndef GCANVAS_H_
#define GCANVAS_H_

#include "gBaseCanvas.h"
#include "gApp.h"
#include "gFont.h"
#include "gRectangle.h"

#include "audio/gVoiceLoopback.h"
#include "voice/VoiceDemoClient.h"
#include "voice/VoiceDemoServer.h"

#include <cstdint>
#include <string>


class gCanvas : public gBaseCanvas {
public:
	explicit gCanvas(gApp* app);
	~gCanvas() override;

	void setup() override;
	void update() override;
	void draw() override;
	void keyPressed(int key) override;
	void keyReleased(int key) override;
	void charPressed(unsigned int codepoint) override;
	void mouseMoved(int x, int y) override;
	void mouseDragged(int x, int y, int button) override;
	void mousePressed(int x, int y, int button) override;
	void mouseReleased(int x, int y, int button) override;
	void mouseScrolled(int x, int y) override;
	void mouseEntered() override;
	void mouseExited() override;
	void windowResized(int w, int h) override;
	void showNotify() override;
	void hideNotify() override;

	void shutdown();

private:
	enum class Screen {
		MENU,
		IP_INPUT,
		PORT_INPUT,
		VOICE,
		LOOPBACK
	};

	enum class ConnectionMode {
		HOST,
		CLIENT
	};

	void drawMenu();
	void drawInput();
	void drawVoice();
	void drawLoopback();
	void startVoice();
	void returnToMenu();
	bool parsePort(std::uint16_t& result) const;

	gApp* app;
	gFont titlefont;
	gFont bodyfont;
	gFont smallfont;
	gRectangle background;
	VoiceDemoServer voiceserver;
	VoiceDemoClient voiceclient;
	gVoiceLoopback loopback;
	Screen screen = Screen::MENU;
	ConnectionMode connectionmode = ConnectionMode::HOST;
	std::string ipinput;
	std::string portinput = "25000";
	std::string message;
	std::string codecselftest;
	bool codecselftestpassed = false;
	bool voicepressed = false;
	bool shutdowncomplete = false;
};

#endif /* GCANVAS_H_ */
