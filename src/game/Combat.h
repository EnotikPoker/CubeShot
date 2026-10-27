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
inline constexpr float explosionRadius = 180;
} // namespace cubeshot
