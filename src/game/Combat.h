#pragma once
#include "game/Types.h"
#include <cstdint>

namespace cubeshot
{
struct FirePatch
{
    std::uint64_t id = 0;
    PlayerId owner = 0;
    Vec2 position;
    float life = 3, radius = 12;
};

inline constexpr unsigned maxFires = 48;
inline constexpr float explosionRadius = 240;
inline constexpr float explosionImpulse = 800;
struct Player;
struct Arena;

struct BlastEffect
{
    Vec2 contact, impulse;
    int damage = 0;
};

int explosionDamage(float distance);
BlastEffect blastEffect(Vec2 at, const Player& player, const Arena& arena, PlayerId direct = 0);
} // namespace cubeshot
