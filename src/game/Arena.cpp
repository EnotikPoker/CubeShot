#include "game/Arena.h"

#include "game/Player.h"

namespace cubeshot
{
const std::vector<MapInfo>& mapCatalog()
{
    static const std::vector<MapInfo> maps{{MapId::Duel, U"Классическая дуэль", MapTheme::Classic},
                                           {MapId::FactorySmall, U"Завод: Цех", MapTheme::Factory},
                                           {MapId::FactoryMedium, U"Завод: Склад", MapTheme::Factory},
                                           {MapId::FactoryLarge, U"Завод: Комплекс", MapTheme::Factory},
                                           {MapId::RuinsSmall, U"Руины: Двор", MapTheme::Ruins},
                                           {MapId::RuinsMedium, U"Руины: Башни", MapTheme::Ruins},
                                           {MapId::RuinsLarge, U"Руины: Крепость", MapTheme::Ruins},
                                           {MapId::NatureSmall, U"Природа: Поляна", MapTheme::Nature},
                                           {MapId::NatureMedium, U"Природа: Террасы", MapTheme::Nature},
                                           {MapId::NatureLarge, U"Природа: Долина", MapTheme::Nature},
                                           {MapId::Test, U"Первый полигон", MapTheme::Classic}};
    return maps;
}

bool validMap(MapId id, bool test)
{
    return static_cast<unsigned>(id) <= static_cast<unsigned>(test ? MapId::Test : MapId::NatureLarge);
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
        return createDefault();
    if (id == MapId::Duel)
    {
        auto a = createDuel();
        if (test)
            a.spawnPoints = {{80, 392}, {832, 392}, {200, 302}, {700, 302}};
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
    return a;
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
