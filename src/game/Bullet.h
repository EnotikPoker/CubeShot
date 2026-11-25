#pragma once

#include "game/ActiveAbilities.h"
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
    unsigned ownerLife = 0, reflections = 0, bounces = 0;
    bool secondary = false, area = false;
    int blastDamage = 0, terrainDamage = 0;
    float blastRadius = 0, terrainRadius = 0;
    float lifetime = 3.0f;
    BulletId id = 0;
    std::uint64_t shotSequence = 0;
    WeaponKind weapon = WeaponKind::Pistol;
    std::uint32_t volley = 0;
    float acceleration = gravity;
    float age = 0;
    float travelled = 0;
    float collisionRadius = radius;
    bool ignites = false, twinned = false;
    unsigned ricochets = 0, penetrations = 0;
    float power = 1, terrainPower = 1, blastScale = 1, rangeScale = 1, spreadScale = 1;
    int impactDamage() const;
    ActiveKind active = ActiveKind::None;
    float boomOutSpeed = 560, boomReturnSpeed = 700, boomRange = 308, returnPower = 1, legTravel = 0;
    Vec2 launchDirection{1, 0};
    unsigned circuits = 0;
    bool returning = false;
    std::uint32_t hitMask = 0;

    void update(float deltaSeconds);
};

std::vector<Bullet> scatterShot(const Bullet& prototype);
} // namespace cubeshot
