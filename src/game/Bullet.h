#pragma once

#include "game/Types.h"

namespace cubeshot {

struct Bullet {
    static constexpr float radius = 3.0f;
    static constexpr float gravity = 240.0f; // Пикселей/с², падение пули.

    Vec2 position; // Центр пули.
    Vec2 velocity;
    int damage = 20;
    PlayerId shooterId = 0;
    float lifetime = 3.0f;
    BulletId id = 0;

    void update(float deltaSeconds);
};

} // namespace cubeshot
