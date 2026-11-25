#pragma once
#include "game/Bullet.h"

namespace cubeshot
{
enum class EntityKind : unsigned
{
    Sticky,
    Mine,
    Screen,
    Barrel
};

struct WorldEntity
{
    std::uint64_t id = 0;
    EntityKind kind = EntityKind::Barrel;
    Vec2 position, velocity, size{16, 16}, anchor{};
    float life = 12, fuse = 0, arm = 0;
    int health = 20;
    PlayerId attached = 0;
    unsigned attachedLife = 0;
    bool anchored = false;
    Bullet attack;

    Rect bounds() const
    {
        return {position, size};
    }
};

inline constexpr unsigned maxWorldEntities = 32;
} // namespace cubeshot
