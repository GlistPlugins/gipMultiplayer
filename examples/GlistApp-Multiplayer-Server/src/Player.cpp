#include "Player.h"

Player::Player(float x, float y, float z)
        : x(x), y(y), z(z) {
    cube.setScale(0.1f);
}

void Player::setCords(float nx, float ny, float nz) {
    x = nx; y = ny; z = nz;
}

void Player::KeyPressed(int key) {
    if (key == G_KEY_W) w = true;
    if (key == G_KEY_S) s = true;
    if (key == G_KEY_A) a = true;
    if (key == G_KEY_D) d = true;
    if (key == G_KEY_Q) q = true;
    if (key == G_KEY_E) e = true;
}

void Player::KeyReleased(int key) {
    if (key == G_KEY_W) w = false;
    if (key == G_KEY_S) s = false;
    if (key == G_KEY_A) a = false;
    if (key == G_KEY_D) d = false;
    if (key == G_KEY_Q) q = false;
    if (key == G_KEY_E) e = false;
}

void Player::Update(float speed) {
    speedX = speedY = speedZ = 0.f;

    if (w) speedZ -= speed;
    if (s) speedZ += speed;
    if (a) speedX -= speed;
    if (d) speedX += speed;
    if (q) speedY += speed;
    if (e) speedY -= speed;

    x += speedX;
    y += speedY;
    z += speedZ;
}

void Player::Draw() {
    cube.setPosition(x, y, z);
    cube.draw();
}