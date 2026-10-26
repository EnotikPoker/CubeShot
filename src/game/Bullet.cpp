#include "game/Bullet.h"
#include <cmath>

namespace cubeshot
{

void Bullet::update(float deltaSeconds)
{
    if (!(deltaSeconds > 0.0f))
    {
        return;
    }
    position.x += velocity.x * deltaSeconds;
    position.y += velocity.y * deltaSeconds + 0.5f * acceleration * deltaSeconds * deltaSeconds;
    velocity.y += acceleration * deltaSeconds;
    lifetime -= deltaSeconds;
}

std::vector<Bullet> scatterShot(const Bullet& prototype)
{
    const auto& definition = weaponDefinition(prototype.weapon);
    std::vector<Bullet> result;
    const float speed = std::hypot(prototype.velocity.x, prototype.velocity.y);
    const float base = std::atan2(prototype.velocity.y, prototype.velocity.x);
    std::uint32_t random = prototype.volley * 747796405u + prototype.shooterId * 2891336453u;
    for (int i = 0; i < definition.pellets; ++i)
    {
        random = random * 1664525u + 1013904223u;
        const float jitter = static_cast<float>(random >> 8) / 16777216.f * 2 - 1;
        const float degrees =
            definition.pellets > 1
                ? definition.spread * (static_cast<float>(i) * 2 / (definition.pellets - 1) - 1) +
                      jitter * 1.2f
                : definition.spread * jitter;
        const float angle = base + degrees * .01745329252f;
        auto bullet = prototype;
        bullet.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
        result.push_back(bullet);
    }
    return result;
}
} // namespace cubeshot
