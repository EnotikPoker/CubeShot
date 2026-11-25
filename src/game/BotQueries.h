#pragma once
#include "game/BotController.h"
#include <cmath>

namespace cubeshot::botdetail
{
inline constexpr float step = 1.f / 120, pi = 3.14159265358979323846f;

inline float distance(Vec2 a, Vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

inline Vec2 center(const BotTarget& p)
{
    return {p.position.x + p.size.x / 2, p.position.y + p.size.y / 2};
}

Player predictedTarget(const BotTarget& target);
Player predictBotTarget(const BotTarget& target, const Player& observer, float seconds, const Arena& arena);
float shotSpeed(const Player& self);
std::optional<BotAim> aimAtPoint(const Player& self, Vec2 point);
BotWeaponProfile abilityProfile(WeaponKind kind, const Abilities& abilities);
float projectileHit(const Player& self, const Player& target, Vec2 direction, const Arena& arena,
                    float horizon);
bool clearShot(const Player& self, Vec2 direction, float time, const Arena& arena);
bool outside(Vec2 p, const Arena& arena);
float terrainHit(Vec2 from, Vec2 to, const Arena& arena, float radius = Bullet::radius);
bool advanceKnownBullet(Bullet& bullet, float age, const Arena& arena, Vec2 owner);
std::optional<BotAim> rocketAim(const Player& self, const BotTarget& target, const Arena& arena, float error);
} // namespace cubeshot::botdetail
