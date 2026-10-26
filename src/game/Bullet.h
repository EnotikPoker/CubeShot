#pragma once

#include "game/Types.h"
#include "game/Weapon.h"
#include <vector>

namespace cubeshot
{

struct Bullet
{
    static constexpr float radius = 3.0f;
    static constexpr float gravity = 240.0f; // Пикселей/с², падение пули.

    Vec2 position; // Центр пули.
    Vec2 velocity;
    int damage = 20;
    PlayerId shooterId = 0;
    float lifetime = 3.0f;
    BulletId id = 0;
    std::uint64_t shotSequence = 0;
    WeaponKind weapon = WeaponKind::Pistol;
    std::uint32_t volley = 0;
    float acceleration = gravity;

    void update(float deltaSeconds);
};

std::vector<Bullet> scatterShot(const Bullet& prototype);
} // namespace cubeshot
