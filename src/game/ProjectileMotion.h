#pragma once
#include "game/Arena.h"
#include "game/BoomerangMotion.h"
#include "game/Bullet.h"
#include "game/Collision.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
inline void bounceProjectile(Bullet& bullet, Vec2 at, Rect surface, float restitution)
{
    const float r = bullet.collisionRadius;
    float dx = std::min(std::abs(at.x - (surface.position.x - r)),
                        std::abs(at.x - (surface.position.x + surface.size.x + r)));
    float dy = std::min(std::abs(at.y - (surface.position.y - r)),
                        std::abs(at.y - (surface.position.y + surface.size.y + r)));
    bool horizontal = dx < dy;
    if (horizontal)
        bullet.velocity.x *= -restitution;
    else
        bullet.velocity.y *= -restitution;
    bullet.position = {at.x + (horizontal ? (bullet.velocity.x > 0 ? .1f : -.1f) : 0.f),
                       at.y + (!horizontal ? (bullet.velocity.y > 0 ? .1f : -.1f) : 0.f)};
}
} // namespace cubeshot

namespace cubeshot
{
inline bool tunnelProjectile(Bullet& bullet, const Arena& arena, Vec2 at, unsigned group, Material material)
{
    if (!bullet.penetrations || !fragile(material))
        return false;
    Rect whole{};
    bool first = true;
    for (const auto& c : arena.terrain)
        if (c.group == group)
        {
            if (first)
            {
                whole = c.bounds;
                first = false;
            }
            else
            {
                float right =
                          std::max(whole.position.x + whole.size.x, c.bounds.position.x + c.bounds.size.x),
                      bottom =
                          std::max(whole.position.y + whole.size.y, c.bounds.position.y + c.bounds.size.y);
                whole.position.x = std::min(whole.position.x, c.bounds.position.x);
                whole.position.y = std::min(whole.position.y, c.bounds.position.y);
                whole.size = {right - whole.position.x, bottom - whole.position.y};
            }
        }
    if (first)
        return false;
    float speed = std::max(.01f, std::hypot(bullet.velocity.x, bullet.velocity.y));
    Vec2 dir{bullet.velocity.x / speed, bullet.velocity.y / speed};
    Vec2 exit = at;
    for (int step = 0; step < 2048; ++step)
    {
        exit.x += dir.x;
        exit.y += dir.y;
        if (exit.x < whole.position.x - bullet.collisionRadius ||
            exit.x > whole.position.x + whole.size.x + bullet.collisionRadius ||
            exit.y < whole.position.y - bullet.collisionRadius ||
            exit.y > whole.position.y + whole.size.y + bullet.collisionRadius)
            break;
    }
    --bullet.penetrations;
    bullet.damage = std::max(1, static_cast<int>(std::lround(
                                    bullet.damage * (bullet.weapon == WeaponKind::Crossbow ? .7f : .6f))));
    bullet.travelled += std::hypot(exit.x - at.x, exit.y - at.y);
    if (!bullet.returning)
        bullet.legTravel += std::hypot(exit.x - at.x, exit.y - at.y);
    bullet.position = exit;
    return true;
}

// Shared terrain-only trajectory for presentation and bot observations. It never mutates the arena.
inline bool advanceProjectileTerrain(Bullet& bullet, const Arena& arena, float dt)
{
    Vec2 from = bullet.position;
    bullet.update(dt);
    Vec2 to = bullet.position;
    if (bullet.lifetime <= 0)
        return false;
    float nearest = 2;
    Rect surface{};
    auto solids = arena.solidsNear(
        {{std::min(from.x, to.x) - bullet.collisionRadius, std::min(from.y, to.y) - bullet.collisionRadius},
         {std::abs(to.x - from.x) + 2 * bullet.collisionRadius,
          std::abs(to.y - from.y) + 2 * bullet.collisionRadius}});
    solids.insert(solids.end(), arena.combatScreens.begin(), arena.combatScreens.end());
    for (auto& r : solids)
        if (auto hit = collision::segmentHit(from, to, r, bullet.collisionRadius); hit && *hit < nearest)
        {
            nearest = *hit;
            surface = r;
        }
    if (nearest <= 1)
    {
        Vec2 at{from.x + (to.x - from.x) * nearest, from.y + (to.y - from.y) * nearest};
        if (bullet.weapon == WeaponKind::GrenadeLauncher)
        {
            bounceProjectile(bullet, at, surface, .55f);
            return true;
        }
        if (auto cell = arena.terrainAt(at, bullet.collisionRadius + 1);
            cell && tunnelProjectile(bullet, arena, at, cell->group, cell->material))
            return true;
        if (bullet.ricochets)
        {
            bounceProjectile(bullet, at, surface, 1);
            --bullet.ricochets;
            ++bullet.bounces;
            bullet.damage = std::max(1, static_cast<int>(std::lround(bullet.damage * .7f)));
            return true;
        }
        bullet.position = at;
        if (bullet.weapon == WeaponKind::FireBoomerang)
        {
            if (bullet.returning)
                return false;
            returnBoomerang(bullet);
            float speed = std::max(.01f, std::hypot(bullet.velocity.x, bullet.velocity.y));
            bullet.position = {at.x - bullet.velocity.x / speed, at.y - bullet.velocity.y / speed};
            return true;
        }
        return false;
    }
    const auto r = bullet.collisionRadius;
    bool inside = to.x >= arena.bounds.position.x + r && to.y >= arena.bounds.position.y + r &&
                  to.x <= arena.bounds.position.x + arena.bounds.size.x - r &&
                  to.y <= arena.bounds.position.y + arena.bounds.size.y - r;
    if (!inside && bullet.weapon == WeaponKind::FireBoomerang && !bullet.returning)
    {
        returnBoomerang(bullet);
        bullet.position = {
            std::clamp(to.x, arena.bounds.position.x + 4, arena.bounds.position.x + arena.bounds.size.x - 4),
            std::clamp(to.y, arena.bounds.position.y + 4, arena.bounds.position.y + arena.bounds.size.y - 4)};
        return true;
    }
    return inside;
}
} // namespace cubeshot
