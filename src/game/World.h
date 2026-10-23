#pragma once

#include "game/Arena.h"
#include "game/Bullet.h"
#include "game/Events.h"
#include "game/Player.h"

#include <vector>

namespace cubeshot
{

class World
{
public:
    static constexpr PlayerId localPlayerId = 1;

    // Explicit player count decouples the game mode from available spawn points.
    // Zero retains support for custom test arenas using every spawn.
    explicit World(Arena arena = Arena::createDefault(), std::size_t playerCount = 0);

    void setPlayerInput(PlayerId id, const PlayerInput& input);
    void clearPlayerInput(PlayerId id);
    void requestAction(PlayerId id, bool jump, bool shoot, Vec2 aim, std::uint64_t sequence = 0);
    void resetPlayers(bool swapSpawns = false);
    void update(float deltaSeconds);

    std::vector<GameEvent> takeEvents();
    const Arena& arena() const;
    const std::vector<Player>& players() const;
    const std::vector<Bullet>& bullets() const;

private:
    bool moveBullet(Bullet& bullet, Vec2 destination);
    void updateBullets(float deltaSeconds);

    void emit(EventKind kind, const Bullet& bullet, Vec2 position, PlayerId target = 0);
    Arena arena_;
    std::size_t playerCount_ = 0;
    std::uint64_t nextEvent_ = 1, tick_ = 0;
    std::vector<GameEvent> events_;
    std::vector<Player> players_;
    std::vector<Bullet> bullets_;
    BulletId nextBulletId_ = 1;
    std::vector<int> pendingDamage_;
};

} // namespace cubeshot
