#include "gAppManager.h"
#include "gApp.h"


int main(int argc, char** argv) {
	gStartEngine(new gApp(argc, argv), "GlistApp Team Voice", G_WINDOWMODE_APP, 1280, 720);
	return 0;
}
