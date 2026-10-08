#include "game/Arena.h"

#include "game/Player.h"

namespace cubeshot {

Arena Arena::createDefault()
{
    Arena arena;
    arena.bounds = {{0.0f, 0.0f}, {960.0f, 540.0f}};
    const Rect floor{{0.0f, 440.0f}, {960.0f, 100.0f}};
    arena.platforms.push_back(floor);
    arena.spawnPoints.push_back({(arena.bounds.size.x - Player::defaultSize) / 2.0f,
                                 floor.position.y - Player::defaultSize});
    return arena;
}

} // namespace cubeshot
