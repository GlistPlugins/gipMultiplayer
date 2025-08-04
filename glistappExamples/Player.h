#ifndef PLAYER_H
#define PLAYER_H
#include "gRectangle.h"

class Player {
public:
    Player(float x, float y, float w, float h);
    void setCords(float x, float y);
    void KeyPressed(int key);
    void KeyReleased(int key);
    void Draw();
    void Update();
    float getX() const { return x; }
    float getY() const { return y; }
    void  setGreen(bool v) { isEmpty = v; }
    bool  getGreen() const { return isEmpty; }
private:
    float x, y, w, h;
    gRectangle rect;
    float speed = 5;
    bool wdown, sdown, adown, ddown;
    bool isEmpty = false;
};

#endif
