#pragma once
#include "game/Types.h"
#include <cstdint>
#include <vector>

namespace cubeshot
{
enum class Material : unsigned
{
    Fixed,
    Wood,
    Glass,
    Stone,
    Metal
};

struct TerrainCell
{
    Rect bounds;
    Material material = Material::Stone;
    std::uint8_t health = 100, maximum = 100;
    unsigned group = 0;
    bool object = false;
};

int materialHealth(Material material);
bool fragile(Material material);

struct TerrainState
{
    std::uint64_t revision = 0;
    std::vector<std::uint8_t> health;
};
} // namespace cubeshot
