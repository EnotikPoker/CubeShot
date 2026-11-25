#include "game/Arena.h"
#include <algorithm>

namespace cubeshot
{
Arena createExpansionMap(MapId id, bool test)
{
    unsigned n = unsigned(id) - unsigned(MapId::LabSectors);
    float w = (n == 0 || n == 2) ? 960.f : (n == 1 || n == 5) ? 1600.f : 1280.f, h = w * 9 / 16, f = h - 100;
    Arena a{{{0, 0}, {w, h}}, {{{0, f}, {w, 100}}}, {{80, f - 48}, {w - 128, f - 48}}, id};
    // Fixed stairs remain traversable even after every destructible piece is gone.
    unsigned levels = w == 960 ? 2 : w == 1280 ? 3 : 4;
    for (unsigned level = 1; level <= levels; ++level)
    {
        float x = 150 + (level - 1) * 125.f, y = f - level * 80.f;
        a.platforms.push_back({{x, y}, {140, 18}});
        a.platforms.push_back({{w - x - 140, y}, {140, 18}});
    }
    if (n == 0)
        a.platforms.push_back({{w / 2 - 70, f - 80}, {140, 18}});
    if (n == 1)
    {
        a.platforms.push_back({{w / 2 - 100, f - 160}, {200, 18}});
        a.platforms.push_back({{w / 2 - 60, f - 240}, {120, 18}});
    }
    if (n == 2)
    {
        a.platforms.push_back({{w / 2 - 100, f - 80}, {200, 18}});
    }
    if (n == 3)
    {
        for (float side : {.36f, .64f})
            a.platforms.push_back({{w * side - 65, f - 160}, {130, 18}});
        a.platforms.push_back({{w / 2 - 70, f - 80}, {140, 18}});
    }
    if (n == 4)
    {
        a.platforms.push_back({{w / 2 - 60, f - 80}, {120, 18}});
        a.platforms.push_back({{w / 2 - 100, f - 160}, {200, 18}});
    }
    if (n == 5)
    {
        a.platforms.push_back({{w / 2 - 70, f - 80}, {140, 18}});
        a.platforms.push_back({{w / 2 - 90, f - 160}, {180, 18}});
        a.platforms.push_back({{w / 2 - 70, f - 240}, {140, 18}});
    }

    a.fixedPlatforms = a.platforms;
    if (test)
    {
        a.spawnPoints.push_back({160, f - 128});
        a.spawnPoints.push_back({w - 208, f - 128});
    }
    Material wall = n < 2 ? Material::Glass : n < 4 ? Material::Wood : Material::Stone;
    for (float side : {.35f, .65f})
    {
        a.addTerrain({{w * side - 12, f - 112}, {24, 112}}, wall);
        a.addTerrain({{w * side - 24, f - 32}, {48, 32}}, Material::Wood, true);
    }
    a.addTerrain({{w / 2 - 50, f - 48}, {100, 48}}, n < 2 ? Material::Glass : Material::Metal, true);
    for (float side : {.22f, .78f})
        a.barrelSpawns.push_back({w * side - 12, f - 28});
    if (n == 1 || n >= 4)
    {
        for (float side : {.42f, .58f})
            a.hazards.push_back({{{w * side - 28, f - 3}, {56, 3}}, n >= 4});
    }
    a.rebuildTerrain();
    return a;
}

std::vector<Vec2> Arena::controlAnchors() const
{
    std::vector<Vec2> out;
    for (float ratio : {.5f, .25f, .75f})
    {
        Vec2 best{bounds.size.x * ratio, bounds.size.y - 124};
        float cost = 1e9f;
        for (auto r : fixedPlatforms)
        {
            if (r.size.x < 60)
                continue;
            float x = std::clamp(bounds.size.x * ratio, r.position.x + 26, r.position.x + r.size.x - 26);
            float c = std::abs(x - bounds.size.x * ratio) +
                      std::abs(r.position.y - (bounds.size.y - (ratio == .5f ? 180 : 100))) * .4f;
            if (c < cost)
            {
                cost = c;
                best = {x, r.position.y - 24};
            }
        }
        out.push_back(best);
    }
    return out;
}

std::vector<Vec2> Arena::respawnPoints() const
{
    std::vector<Vec2> out = spawnPoints;
    for (float ratio : {.25f, .75f})
    {
        Vec2 pos{bounds.size.x * ratio - 24, bounds.size.y - 148};
        for (auto r : fixedPlatforms)
            if (r.position.x <= pos.x && r.position.x + r.size.x >= pos.x + 48)
                pos.y = std::min(pos.y, r.position.y - 48);
        out.push_back(pos);
    }
    return out;
}
} // namespace cubeshot
