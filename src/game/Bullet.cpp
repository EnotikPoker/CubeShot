#include "game/Bullet.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{

void Bullet::update(float deltaSeconds)
{
    if (!(deltaSeconds > 0.0f) || !std::isfinite(deltaSeconds))
    {
        return;
    }
    float remaining = deltaSeconds;
    const auto& d = weaponDefinition(weapon);
    const bool ordinary = !meleeWeapon(weapon) && weapon != WeaponKind::Rpg &&
                          weapon != WeaponKind::Flamethrower && weapon != WeaponKind::FireBoomerang;
    while (remaining > 0)
    {
        const float dt = std::min(remaining, 1.f / 120);
        remaining -= dt;
        const bool spent = ordinary && d.effectiveRange > 0 && travelled >= d.effectiveRange;
        if (spent)
        {
            float drag = std::exp(-2 * dt);
            velocity.x *= drag;
            velocity.y *= drag;
        }
        const float g = acceleration * (spent ? 4.f : 1.f);
        const Vec2 delta{velocity.x * dt, velocity.y * dt + .5f * g * dt * dt};
        position.x += delta.x;
        position.y += delta.y;
        travelled += std::hypot(delta.x, delta.y);
        velocity.y += g * dt;
        lifetime -= dt;
        age += dt;
    }
}

int Bullet::impactDamage() const
{
    const auto& d = weaponDefinition(weapon);
    float factor = 1;
    if (weapon == WeaponKind::Flamethrower)
        factor = std::clamp(lifetime / .12f, 0.f, 1.f);
    else if (!meleeWeapon(weapon) && weapon != WeaponKind::Rpg && weapon != WeaponKind::FireBoomerang &&
             d.effectiveRange > 0)
        factor = std::clamp(1 - .9f * (travelled - d.effectiveRange) / (.75f * d.effectiveRange), .1f, 1.f);
    return std::max(1, static_cast<int>(std::lround(damage * factor)));
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
