#include "game/Bullet.h"

namespace cubeshot {

void Bullet::update(float deltaSeconds)
{
    if (!(deltaSeconds > 0.0f)) {
        return;
    }
    position.x += velocity.x * deltaSeconds;
    position.y += velocity.y * deltaSeconds + 0.5f * gravity * deltaSeconds * deltaSeconds;
    velocity.y += gravity * deltaSeconds;
    lifetime -= deltaSeconds;
}

} // namespace cubeshot
