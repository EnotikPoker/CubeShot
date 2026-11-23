#pragma once
#include "game/Arena.h"
#include <algorithm>
#include <map>

// Navigation-only tests isolate the permanent routes from shootable barriers.
// Full decorated maps and destruction are exercised by bot_match/destruction tests.
inline cubeshot::Arena openRoutes(cubeshot::MapId id)
{
    auto arena = cubeshot::Arena::create(id);
    std::map<unsigned, cubeshot::Rect> groups;
    for (auto& c : arena.terrain)
        if (!c.object)
        {
            auto [it, inserted] = groups.emplace(c.group, c.bounds);
            if (inserted)
                continue;
            auto& r = it->second;
            float right = std::max(r.position.x + r.size.x, c.bounds.position.x + c.bounds.size.x),
                  bottom = std::max(r.position.y + r.size.y, c.bounds.position.y + c.bounds.size.y);
            r.position.x = std::min(r.position.x, c.bounds.position.x);
            r.position.y = std::min(r.position.y, c.bounds.position.y);
            r.size = {right - r.position.x, bottom - r.position.y};
        }
    arena.platforms = arena.fixedPlatforms;
    for (auto entry : groups)
        if (entry.second.size.x >= entry.second.size.y)
            arena.platforms.push_back(entry.second);
    arena.terrain.clear();
    arena.materials.clear();
    arena.spatial.clear();
    arena.fixedPlatforms.clear();
    return arena;
}
