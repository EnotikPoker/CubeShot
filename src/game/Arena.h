#pragma once

#include "game/Types.h"

#include <string_view>
#include <vector>

namespace cubeshot
{

enum class MapId : std::uint32_t
{
    Duel = 0,
    FactorySmall,
    FactoryMedium,
    FactoryLarge,
    RuinsSmall,
    RuinsMedium,
    RuinsLarge,
    NatureSmall,
    NatureMedium,
    NatureLarge,
    Test
};
enum class MapTheme
{
    Classic,
    Factory,
    Ruins,
    Nature
};

struct MapInfo
{
    MapId id;
    std::u32string_view name;
    MapTheme theme;
};

const std::vector<MapInfo>& mapCatalog();
bool validMap(MapId id, bool test = false);
const MapInfo& mapInfo(MapId id);

struct Arena
{
    Rect bounds;
    std::vector<Rect> platforms;
    std::vector<Vec2> spawnPoints; // Положение верхнего левого угла персонажа.

    MapId map = MapId::Test;
    static Arena create(MapId map, bool test = false);
    static Arena createDefault();
    static Arena createDuel();
};

} // namespace cubeshot
