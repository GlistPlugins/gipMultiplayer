#include "Player.h"

Player::Player(float x, float y, float w, float h)
    : x(x), y(y), w(w), h(h), rect(x, y, w, h, true),
      speed(5.f), wdown(false), sdown(false), adown(false), ddown(false) {}

void Player::setCords(float nx, float ny) {
    x = nx; y = ny;
    rect.setPoints(x, y, w, h, true);
}

void Player::KeyPressed(int key) {
    if (key == G_KEY_W) wdown = true;
    else if (key == G_KEY_S) sdown = true;
    if (key == G_KEY_A) adown = true;
    else if (key == G_KEY_D) ddown = true;
    if (key == G_KEY_R) isEmpty = true;
}

void Player::KeyReleased(int key) {
    if (key == G_KEY_W) wdown = false;
    else if (key == G_KEY_S) sdown = false;
    if (key == G_KEY_A) adown = false;
    else if (key == G_KEY_D) ddown = false;
    if (key == G_KEY_R) isEmpty = false;
}

void Player::Update() {
    if (wdown) y -= speed;
    if (sdown) y += speed;
    if (adown) x -= speed;
    if (ddown) x += speed;

    rect.setPoints(x, y, w, h, true);
}

void Player::Draw() {
    if (isEmpty) rect.draw(x, y, w, h, false);
    else rect.draw(x, y, w, h, true);
}