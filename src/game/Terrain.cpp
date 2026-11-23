#include "game/Arena.h"
#include "game/Collision.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace cubeshot
{
int materialHealth(Material m)
{
    switch (m)
    {
        case Material::Wood:
            return 40;
        case Material::Glass:
            return 10;
        case Material::Metal:
            return 120;
        default:
            return 100;
    }
}

bool fragile(Material m)
{
    return m == Material::Wood || m == Material::Glass;
}

void Arena::addTerrain(Rect box, Material material, bool object)
{
    if (object && std::count_if(terrain.begin(), terrain.end(), [](const auto& c) { return c.object; }) >= 24)
        throw std::invalid_argument("Terrain exceeds 24 cover objects");
    if (object)
        for (auto spawn : spawnPoints)
            if (collision::overlaps(box, {spawn, {48, 48}}))
                return;
    if (terrain.empty() && fixedPlatforms.empty())
        fixedPlatforms = platforms;
    const unsigned group = nextTerrainGroup++;
    const int hp = object && material == Material::Metal ? 60 : materialHealth(material);
    const float step = object ? std::max(box.size.x, box.size.y) : 8.f;
    for (float y = 0; y < box.size.y; y += step)
        for (float x = 0; x < box.size.x; x += step)
        {
            if (terrain.size() >= 4096)
                throw std::invalid_argument("Terrain exceeds 4096 cells");
            terrain.push_back({{{box.position.x + x, box.position.y + y},
                                {std::min(step, box.size.x - x), std::min(step, box.size.y - y)}},
                               material,
                               static_cast<std::uint8_t>(hp),
                               static_cast<std::uint8_t>(hp),
                               group,
                               object});
        }
}

void Arena::rebuildTerrain()
{
    if (terrain.empty())
        return;
    platforms = fixedPlatforms;
    materials.assign(platforms.size(), Material::Fixed);
    // Merge horizontal cells and then equal adjacent rows: collisions stay compact.
    for (const auto& c : terrain)
        if (c.health)
        {
            bool merged = false;
            if (platforms.size() > fixedPlatforms.size())
            {
                auto& r = platforms.back();
                if (materials.back() == c.material && r.position.y == c.bounds.position.y &&
                    r.size.y == c.bounds.size.y &&
                    std::abs(r.position.x + r.size.x - c.bounds.position.x) < .01f)
                {
                    r.size.x += c.bounds.size.x;
                    merged = true;
                }
            }
            if (!merged)
            {
                platforms.push_back(c.bounds);
                materials.push_back(c.material);
            }
        }
    auto runs = std::move(platforms);
    auto runMaterials = std::move(materials);
    platforms = fixedPlatforms;
    materials.assign(platforms.size(), Material::Fixed);
    std::map<std::tuple<float, float, Material>, std::size_t> last;
    for (std::size_t i = fixedPlatforms.size(); i < runs.size(); ++i)
    {
        auto key = std::make_tuple(runs[i].position.x, runs[i].size.x, runMaterials[i]);
        auto found = last.find(key);
        if (found != last.end() && std::abs(platforms[found->second].position.y +
                                            platforms[found->second].size.y - runs[i].position.y) < .01f)
            platforms[found->second].size.y += runs[i].size.y;
        else
        {
            last[key] = platforms.size();
            platforms.push_back(runs[i]);
            materials.push_back(runMaterials[i]);
        }
    }
    ++geometryRevision;
    spatial.clear();
    for (std::size_t i = 0; i < platforms.size(); ++i)
    {
        const auto& r = platforms[i];
        for (int y = static_cast<int>(std::floor(r.position.y / 64));
             y <= static_cast<int>(std::floor((r.position.y + r.size.y) / 64)); ++y)
            for (int x = static_cast<int>(std::floor(r.position.x / 64));
                 x <= static_cast<int>(std::floor((r.position.x + r.size.x) / 64)); ++x)
                spatial[{x, y}].push_back(i);
    }
}

std::vector<Rect> Arena::solidsNear(Rect box) const
{
    if (spatial.empty())
        return platforms;
    std::vector<std::size_t> ids;
    for (int y = static_cast<int>(std::floor(box.position.y / 64));
         y <= static_cast<int>(std::floor((box.position.y + box.size.y) / 64)); ++y)
        for (int x = static_cast<int>(std::floor(box.position.x / 64));
             x <= static_cast<int>(std::floor((box.position.x + box.size.x) / 64)); ++x)
        {
            auto f = spatial.find({x, y});
            if (f != spatial.end())
                ids.insert(ids.end(), f->second.begin(), f->second.end());
        }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<Rect> result;
    for (auto i : ids)
        result.push_back(platforms[i]);
    return result;
}

void Arena::restoreTerrain()
{
    if (terrain.empty())
        return;
    for (auto& c : terrain)
        c.health = c.maximum;
    ++terrainRevision;
    rebuildTerrain();
}

TerrainState Arena::terrainState() const
{
    TerrainState s;
    s.revision = terrainRevision;
    for (auto& c : terrain)
        s.health.push_back(c.health);
    return s;
}

bool Arena::applyTerrain(const TerrainState& state)
{
    if (state.health.size() != terrain.size())
        return false;
    for (std::size_t i = 0; i < terrain.size(); ++i)
        if (state.health[i] > terrain[i].maximum)
            return false;
    if (state.revision == terrainRevision && state.health == terrainState().health)
        return true;
    bool topology = false;
    for (std::size_t i = 0; i < terrain.size(); ++i)
    {
        topology |= (terrain[i].health == 0) != (state.health[i] == 0);
        terrain[i].health = state.health[i];
    }
    terrainRevision = state.revision;
    if (topology)
        rebuildTerrain();
    return true;
}

bool Arena::damageTerrain(Vec2 at, float radius, int damage, bool fire, bool explosion)
{
    bool changed = false, topology = false;
    for (auto& c : terrain)
    {
        if (!c.health || (fire && c.material != Material::Wood))
            continue;
        float x = std::clamp(at.x, c.bounds.position.x, c.bounds.position.x + c.bounds.size.x),
              y = std::clamp(at.y, c.bounds.position.y, c.bounds.position.y + c.bounds.size.y);
        float distance = std::hypot(at.x - x, at.y - y);
        if (distance > radius)
            continue;
        int amount =
            std::max(1, static_cast<int>(std::lround(
                            damage * (c.material == Material::Metal && !fire && !explosion ? 1.25f : 1.f) *
                            (1 - distance / std::max(radius, .01f)))));
        c.health = static_cast<std::uint8_t>(std::max(0, int(c.health) - amount));
        changed = true;
        topology |= c.health == 0;
    }
    if (changed)
    {
        ++terrainRevision;
        if (topology)
            rebuildTerrain();
    }
    return changed;
}

const TerrainCell* Arena::terrainAt(Vec2 at, float radius) const
{
    for (auto& c : terrain)
        if (c.health && at.x >= c.bounds.position.x - radius &&
            at.x <= c.bounds.position.x + c.bounds.size.x + radius && at.y >= c.bounds.position.y - radius &&
            at.y <= c.bounds.position.y + c.bounds.size.y + radius)
            return &c;
    return nullptr;
}

void Arena::decorate()
{
    const auto original = platforms;
    fixedPlatforms.clear();
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        if (i && (map == MapId::Duel || static_cast<int>(original[i].position.y / 10) % 2 == 0 ||
                  static_cast<unsigned>(map) >= 11))
        {
            Material m = mapInfo(map).theme == MapTheme::Nature  ? Material::Wood
                         : mapInfo(map).theme == MapTheme::Ruins ? Material::Stone
                                                                 : Material::Metal;
            addTerrain(original[i], m);
        }
        else
            fixedPlatforms.push_back(original[i]);
    }
    const float floor = bounds.size.y - 100, w = bounds.size.x;
    if (map == MapId::Duel)
    {
        for (float x : {200.f, 728.f})
            addTerrain({{x, 318}, {32, 32}}, Material::Wood, true);
        for (float x : {280.f, 656.f})
            addTerrain({{x, 310}, {24, 40}}, Material::Glass, true);
        for (float x : {150.f, 782.f})
            addTerrain({{x, 322}, {28, 28}}, Material::Metal, true);
        rebuildTerrain();
        return;
    }
    for (float f : {.35f, .65f})
    {
        addTerrain({{w * f - 16, floor - 32}, {32, 32}}, Material::Wood, true);
        addTerrain({{w * f + (f < .5f ? -70.f : 42.f), floor - 40}, {24, 40}}, Material::Glass, true);
    }
    addTerrain({{w * .5f - 14, floor - 28}, {28, 28}}, Material::Metal, true);
    rebuildTerrain();
}
} // namespace cubeshot
