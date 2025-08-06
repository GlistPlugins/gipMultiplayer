#pragma once

#include "gBox.h"

class Player {
public:
    Player(float x, float y, float z);

    void setCords(float x, float y, float z);

    void KeyPressed(int key);
    void KeyReleased(int key);

    void Update(float speed);
    void Draw();

    float getX() const { return x; }
    float getY() const { return y; }
    float getZ() const { return z; }

private:
    gBox cube;

    float x{}, y{}, z{};
    float speedX{}, speedY{}, speedZ{};
    bool w{}, s{}, a{}, d{}, q{}, e{};
};