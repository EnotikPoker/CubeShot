#pragma once

#include "game/Modes.h"
#include "game/Terrain.h"
#include "game/Types.h"
#include <map>

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
    Test,
    FactoryPartitions,
    RuinsBreach,
    NatureCrossing,
    LabSectors,
    LabReactor,
    DocksLoading,
    DocksContainers,
    Foundry,
    Mine
};
enum class MapTheme
{
    Classic,
    Factory,
    Ruins,
    Nature,
    Laboratory,
    Docks,
    Foundry
};

struct MapInfo
{
    MapId id;
    std::u32string_view name;
    MapTheme theme;
};

const std::vector<MapInfo>& mapCatalog();
bool validMap(MapId id, bool test = false);
std::vector<MapId> playableMaps(bool test = false);
const MapInfo& mapInfo(MapId id);

struct MapHazard
{
    Rect bounds;
    bool heat = false;
};

struct Arena
{
    Rect bounds;
    std::vector<Rect> platforms;
    std::vector<Vec2> spawnPoints; // Положение верхнего левого угла персонажа.

    MapId map = MapId::Test;
    std::vector<Rect> fixedPlatforms;
    std::vector<Material> materials;
    std::vector<TerrainCell> terrain;
    std::uint64_t terrainRevision = 0, geometryRevision = 0;
    unsigned nextTerrainGroup = 1;
    std::map<std::pair<int, int>, std::vector<std::size_t>> spatial;
    SimulationModifiers modifiers;
    std::vector<MapHazard> hazards;
    std::vector<Vec2> barrelSpawns;
    std::vector<Rect> combatScreens;
    std::vector<Vec2> controlAnchors() const;
    std::vector<Vec2> respawnPoints() const;
    void addTerrain(Rect box, Material material, bool object = false);
    void decorate();
    void rebuildTerrain();
    void restoreTerrain();
    TerrainState terrainState() const;
    bool applyTerrain(const TerrainState& state);
    bool damageTerrain(Vec2 at, float radius, int damage, bool fire = false, bool explosion = false);
    const TerrainCell* terrainAt(Vec2 at, float radius = 1) const;
    std::vector<Rect> solidsNear(Rect bounds) const;
    static Arena create(MapId map, bool test = false);
    static Arena createDefault();
    static Arena createDuel();
};

} // namespace cubeshot
