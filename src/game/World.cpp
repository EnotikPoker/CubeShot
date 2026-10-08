#include "game/World.h"

namespace cubeshot {

World::World() : arena_(Arena::createDefault())
{
    Player player;
    player.id = localPlayerId;
    player.position = arena_.spawnPoints.front();
    players_.push_back(player);
}

void World::setPlayerInput(PlayerId id, const PlayerInput& input)
{
    for (Player& player : players_) {
        if (player.id == id) {
            player.applyInput(input);
            return;
        }
    }
}

void World::update(float deltaSeconds)
{
    for (Player& player : players_) {
        player.update(deltaSeconds, arena_);
    }
}

const Arena& World::arena() const
{
    return arena_;
}

const std::vector<Player>& World::players() const
{
    return players_;
}

const std::vector<Bullet>& World::bullets() const
{
    return bullets_;
}

} // namespace cubeshot
