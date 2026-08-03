#ifndef GAPP_H_
#define GAPP_H_

#include "gBaseApp.h"


class gCanvas;

class gApp : public gBaseApp {
public:
	gApp();
	gApp(int argc, char** argv);
	~gApp() override;

	void setup() override;
	void update() override;
	void stop() override;

private:
	gCanvas* canvas = nullptr;
};

#endif /* GAPP_H_ */
