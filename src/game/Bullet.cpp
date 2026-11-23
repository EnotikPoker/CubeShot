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
    const bool ordinary = active == ActiveKind::None && ballisticWeapon(weapon);
    while (remaining > 0)
    {
        const float dt = std::min(remaining, 1.f / 120);
        remaining -= dt;
        const bool spent = ordinary && d.effectiveRange > 0 && travelled >= d.effectiveRange * rangeScale;
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
        if (!returning)
            legTravel += std::hypot(delta.x, delta.y);
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
    else if (active == ActiveKind::None && ballisticWeapon(weapon) && d.effectiveRange > 0)
        factor = std::clamp(1 - .9f * (travelled - d.effectiveRange * rangeScale) /
                                    (.75f * d.effectiveRange * rangeScale),
                            .1f, 1.f);
    if (weapon == WeaponKind::FireBoomerang && returning)
        factor *= returnPower;
    return std::max(1, static_cast<int>(std::lround(damage * factor)));
}

std::vector<Bullet> scatterShot(const Bullet& prototype)
{
    const auto& definition = weaponDefinition(prototype.weapon);
    std::vector<Bullet> result;
    const float speed = std::hypot(prototype.velocity.x, prototype.velocity.y);
    const float base = std::atan2(prototype.velocity.y, prototype.velocity.x);
    std::uint32_t random = prototype.volley * 747796405u + prototype.shooterId * 2891336453u;
    const int count = definition.pellets * (prototype.twinned ? 2 : 1);
    for (int i = 0; i < count; ++i)
    {
        random = random * 1664525u + 1013904223u;
        const float jitter = static_cast<float>(random >> 8) / 16777216.f * 2 - 1;
        const float degrees =
            count > 1 ? definition.spread * (static_cast<float>(i) * 2 / (count - 1) - 1) + jitter * 1.2f
                      : definition.spread * jitter;
        const float angle = base + degrees * prototype.spreadScale * .01745329252f;
        auto bullet = prototype;
        if (prototype.twinned)
            bullet.damage = std::max(1, static_cast<int>(std::lround(bullet.damage * .65f)));
        bullet.velocity = {std::cos(angle) * speed, std::sin(angle) * speed};
        if (bullet.weapon == WeaponKind::FireBoomerang)
            bullet.launchDirection = {std::cos(angle), std::sin(angle)};
        result.push_back(bullet);
    }
    return result;
}
} // namespace cubeshot
