#include "game/Arena.h"

#include "game/Collision.h"
#include "game/Player.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
Arena createExpansionMap(MapId, bool);

const std::vector<MapInfo>& mapCatalog()
{
    static const std::vector<MapInfo> maps{
        {MapId::Duel, U"Классическая дуэль", MapTheme::Classic},
        {MapId::FactorySmall, U"Завод: Цех", MapTheme::Factory},
        {MapId::FactoryMedium, U"Завод: Склад", MapTheme::Factory},
        {MapId::FactoryLarge, U"Завод: Комплекс", MapTheme::Factory},
        {MapId::RuinsSmall, U"Руины: Двор", MapTheme::Ruins},
        {MapId::RuinsMedium, U"Руины: Башни", MapTheme::Ruins},
        {MapId::RuinsLarge, U"Руины: Крепость", MapTheme::Ruins},
        {MapId::NatureSmall, U"Природа: Поляна", MapTheme::Nature},
        {MapId::NatureMedium, U"Природа: Террасы", MapTheme::Nature},
        {MapId::NatureLarge, U"Природа: Долина", MapTheme::Nature},
        {MapId::Test, U"Первый полигон", MapTheme::Classic},
        {MapId::FactoryPartitions, U"Завод: Перегородки", MapTheme::Factory},
        {MapId::RuinsBreach, U"Руины: Пролом", MapTheme::Ruins},
        {MapId::NatureCrossing, U"Природа: Переправа", MapTheme::Nature},
        {MapId::LabSectors, U"Лаборатория: Сектора", MapTheme::Laboratory},
        {MapId::LabReactor, U"Лаборатория: Реактор", MapTheme::Laboratory},
        {MapId::DocksLoading, U"Доки: Погрузочная", MapTheme::Docks},
        {MapId::DocksContainers, U"Доки: Контейнеры", MapTheme::Docks},
        {MapId::Foundry, U"Литейная", MapTheme::Foundry},
        {MapId::Mine, U"Литейная: Шахта", MapTheme::Foundry},
        {MapId::Crossroads, U"Перекрёсток", MapTheme::Factory},
        {MapId::Terraces, U"Высокие террасы", MapTheme::Nature}};
    return maps;
}

bool validMap(MapId id, bool test)
{
    return static_cast<unsigned>(id) <= static_cast<unsigned>(MapId::Terraces) && (test || id != MapId::Test);
}

std::vector<MapId> playableMaps(bool test)
{
    std::vector<MapId> ids;
    for (auto& m : mapCatalog())
        if (validMap(m.id, test))
            ids.push_back(m.id);
    return ids;
}

const MapInfo& mapInfo(MapId id)
{
    return mapCatalog()[validMap(id, true) ? static_cast<unsigned>(id) : 0];
}

Arena Arena::create(MapId id, bool test)
{
    if (!validMap(id, test))
        id = MapId::Duel;
    if (id == MapId::Test)
    {
        auto a = createDefault();
        a.decorate();
        return a;
    }
    if (id == MapId::Crossroads || id == MapId::Terraces)
    {
        bool tall = id == MapId::Terraces;
        Arena a = create(tall ? MapId::FactoryLarge : MapId::FactoryMedium, false);
        a.map = id;
        // Retain the full proven horizontal stair routes as permanent supports.
        std::map<unsigned, Rect> routes;
        for (auto cell : a.terrain)
            if (!cell.object)
            {
                auto [it, added] = routes.emplace(cell.group, cell.bounds);
                if (!added)
                {
                    auto& b = it->second;
                    float right =
                              std::max(b.position.x + b.size.x, cell.bounds.position.x + cell.bounds.size.x),
                          bottom =
                              std::max(b.position.y + b.size.y, cell.bounds.position.y + cell.bounds.size.y);
                    b.position.x = std::min(b.position.x, cell.bounds.position.x);
                    b.position.y = std::min(b.position.y, cell.bounds.position.y);
                    b.size = {right - b.position.x, bottom - b.position.y};
                }
            }
        a.platforms = a.fixedPlatforms;
        for (auto [group, box] : routes)
            if (box.size.x >= box.size.y)
                a.platforms.push_back(box);
        a.terrain.clear();
        a.materials.clear();
        a.spatial.clear();
        a.nextTerrainGroup = 1;
        float w = a.bounds.size.x, f = a.bounds.size.y - 100;
        a.fixedPlatforms = a.platforms;
        for (float side : {.32f, .68f})
        {
            a.addTerrain({{w * side - 12, f - 100}, {24, 100}}, Material::Wood);
            a.addTerrain({{w * side - 30, f - 32}, {60, 32}}, Material::Glass, true);
        }
        a.addTerrain({{w / 2 - 40, f - 48}, {80, 48}}, Material::Stone, true);
        a.rebuildTerrain();
        if (test)
            a.spawnPoints = a.combatSpawns();
        return a;
    }
    if (unsigned(id) >= unsigned(MapId::LabSectors))
        return createExpansionMap(id, test);
    if (id == MapId::FactoryPartitions || id == MapId::RuinsBreach || id == MapId::NatureCrossing)
    {
        MapId base = id == MapId::FactoryPartitions ? MapId::FactorySmall
                     : id == MapId::RuinsBreach     ? MapId::RuinsMedium
                                                    : MapId::NatureLarge;
        auto a = create(base, test);
        a.map = id;
        // New layouts retain safe floors but open alternative routes through walls and bridges.
        a.terrain.clear();
        a.materials.clear();
        a.spatial.clear();
        a.platforms = a.fixedPlatforms;
        a.nextTerrainGroup = 1;
        float w = a.bounds.size.x, f = a.bounds.size.y - 100;
        a.fixedPlatforms = {
            {{0, f}, {w, 100}}, {{w * .32f - 100, f - 80}, {90, 20}}, {{w * .68f + 10, f - 80}, {90, 20}}};
        if (test)
        {
            a.spawnPoints[2] = {w * .32f - 88, f - 128};
            a.spawnPoints[3] = {w * .68f + 22, f - 128};
        }
        a.addTerrain({{w * .32f, f - 150}, {24, 150}},
                     id == MapId::RuinsBreach ? Material::Stone : Material::Wood);
        a.addTerrain({{w * .68f - 24, f - 150}, {24, 150}},
                     id == MapId::RuinsBreach ? Material::Stone : Material::Glass);
        a.addTerrain({{w * .32f, f - 160}, {w * .36f, 24}},
                     id == MapId::NatureCrossing ? Material::Wood : Material::Stone);
        for (float ratio : {.25f, .75f})
            a.addTerrain({{w * ratio - 18, f - 36}, {36, 36}}, Material::Wood, true);
        a.addTerrain({{w * .5f - 16, f - 32}, {32, 32}}, Material::Metal, true);
        a.rebuildTerrain();
        return a;
    }
    if (id == MapId::Duel)
    {
        auto a = createDuel();
        if (test)
            a.spawnPoints = {{80, 392}, {832, 392}, {200, 302}, {700, 302}};
        a.decorate();
        return a;
    }
    const unsigned index = static_cast<unsigned>(id) - 1;
    const unsigned size = index % 3, theme = index / 3;
    const float w = size == 0 ? 960.f : size == 1 ? 1280.f : 1600.f;
    const float h = w * 9 / 16, floor = h - 100;
    Arena a;
    a.map = id;
    a.bounds = {{0, 0}, {w, h}};
    a.platforms.push_back({{0, floor}, {w, 100}});
    auto ledge = [&](float x, float y, float length) { a.platforms.push_back({{x, y}, {length, 20}}); };
    // Mirrored routes, with rises <= 80 and gaps <= 140 at every jump.
    // Additional levels vary by theme as well as size; they are not scaled copies.
    if (theme == 0)
    {
        ledge(w / 2 - 45, floor - 70, 90);
        for (unsigned level = 1; level <= size + 1; ++level)
        {
            float x = 100 + (level - 1) * 135.f, y = floor - level * 80.f;
            ledge(x, y, 150);
            ledge(w - x - 150, y, 150);
        }
        if (size)
            ledge(w / 2 - 110, floor - 160, 220);
        if (size == 2)
        {
            ledge(w / 2 - 180, floor - 240, 120);
            ledge(w / 2 + 60, floor - 240, 120);
        }
    }
    else if (theme == 1)
    {
        for (unsigned level = 1; level <= size + 2; ++level)
        {
            float x = 125 + (level % 2) * 95.f, y = floor - level * 75.f;
            ledge(x, y, 180);
            ledge(w - x - 180, y, 180);
        }
        ledge(w / 2 - 70, floor - 75, 140);
        if (size)
            ledge(w / 2 - 110, floor - 150, 220);
        if (size == 2)
        {
            ledge(w / 2 - 300, floor - 150, 180);
            ledge(w / 2 + 120, floor - 150, 180);
            ledge(w / 2 - 190, floor - 225, 380);
        }
    }
    else
    {
        for (unsigned level = 1; level <= size + 2; ++level)
        {
            float x = 90 + (level - 1) * 115.f, y = floor - level * 70.f;
            ledge(x, y, 175);
            ledge(w - x - 175, y, 175);
        }
        if (size == 0)
            ledge(w / 2 - 55, floor - 75, 110);
        if (size == 1)
            ledge(w / 2 - 85, floor - 70, 170);
        if (size == 2)
        {
            ledge(w / 2 - 100, floor - 80, 200);
            ledge(w / 2 - 100, floor - 160, 200);
        }
    }
    a.spawnPoints = {{36, floor - 48}, {w - 84, floor - 48}};
    if (test)
        a.spawnPoints = {{36, floor - 48},
                         {w - 84, floor - 48},
                         {a.platforms[1].position.x + 12, a.platforms[1].position.y - 48},
                         {w / 2 - 24, floor - 48}};
    a.decorate();
    return a;
}

std::vector<Vec2> Arena::combatSpawns() const
{
    std::vector<Vec2> out;
    const float w = bounds.size.x, h = bounds.size.y;
    // Search supported, unobstructed bodies on both sides, avoiding barrels/hazards.
    for (float ratio : {.06f, .94f, .25f, .75f})
    {
        Vec2 best{bounds.position.x + w * ratio - 24, bounds.position.y + h - 148};
        float cost = 1e20f;
        for (auto floor : fixedPlatforms.empty() ? platforms : fixedPlatforms)
        {
            if (floor.size.x < 50)
                continue;
            for (float x = floor.position.x + 2; x + 48 <= floor.position.x + floor.size.x - 2; x += 12)
            {
                Vec2 at{x, floor.position.y - 48};
                Rect body{at, {48, 48}};
                bool safe = true;
                for (auto wall : platforms)
                    if (collision::overlaps(body, wall))
                    {
                        safe = false;
                        break;
                    }
                for (auto p : out)
                    if (std::hypot(at.x - p.x, at.y - p.y) < 90)
                        safe = false;
                for (auto p : barrelSpawns)
                    if (std::hypot(at.x - p.x, at.y - p.y) < 165)
                        safe = false;
                for (auto hazard : hazards)
                    if (collision::overlaps({{at.x - 12, at.y}, {72, 56}}, hazard.bounds))
                        safe = false;
                if (!safe)
                    continue;
                float c =
                    std::abs(x - (bounds.position.x + w * ratio - 24)) + (h - 100 - floor.position.y) * .2f;
                if (c < cost)
                {
                    cost = c;
                    best = at;
                }
            }
        }
        out.push_back(best);
    }
    return out;
}

Arena Arena::createDuel()
{
    return {{{0, 0}, {960, 540}},
            {{{0, 440}, {960, 100}}, {{140, 350}, {200, 24}}, {{620, 350}, {200, 24}}},
            {{80, 392}, {832, 392}},
            MapId::Duel};
}

Arena Arena::createDefault()
{
    Arena arena;
    arena.bounds = {{0.0f, 0.0f}, {960.0f, 540.0f}};
    const Rect floor{{0.0f, 440.0f}, {960.0f, 100.0f}};
    arena.platforms.push_back(floor);
    arena.platforms.push_back({{620.0f, 350.0f}, {200.0f, 24.0f}});
    arena.spawnPoints.push_back(
        {(arena.bounds.size.x - Player::defaultSize) / 2.0f, floor.position.y - Player::defaultSize});
    arena.spawnPoints.push_back({140.0f, 392.0f});
    arena.spawnPoints.push_back({860.0f, 392.0f});
    arena.spawnPoints.push_back({710.0f, 302.0f});
    return arena;
}

} // namespace cubeshot
