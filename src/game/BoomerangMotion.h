#pragma once
#include "game/Bullet.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
inline void returnBoomerang(Bullet& b)
{
    if (!b.returning)
    {
        b.returning = true;
        b.hitMask = 0;
    }
}

inline bool steerBoomerang(Bullet& b, Vec2 owner, bool alive = true)
{
    if (!b.returning && b.legTravel >= b.boomRange)
        returnBoomerang(b);
    if (b.returning)
    {
        float dx = owner.x - b.position.x, dy = owner.y - b.position.y, n = std::hypot(dx, dy);
        if (n < 24)
        {
            if (!b.circuits || !alive)
                return false;
            --b.circuits;
            b.returning = false;
            b.hitMask = 0;
            b.legTravel = 0;
            b.position = owner;
            b.velocity = {b.launchDirection.x * b.boomOutSpeed, b.launchDirection.y * b.boomOutSpeed};
        }
        else
            b.velocity = {dx / n * b.boomReturnSpeed, dy / n * b.boomReturnSpeed};
    }
    return b.lifetime > 0;
}
} // namespace cubeshot
