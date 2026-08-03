#include "gApp.h"

#include "gCanvas.h"


gApp::gApp() {
}

gApp::gApp(int argc, char** argv) : gBaseApp(argc, argv) {
}

gApp::~gApp() {
}

void gApp::setup() {
	canvas = new gCanvas(this);
	setCurrentCanvas(canvas);
}

void gApp::update() {
}

void gApp::stop() {
	if (canvas) canvas->shutdown();
}
