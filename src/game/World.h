#pragma once

#include "game/Arena.h"
#include "game/Bullet.h"
#include "game/Player.h"

#include <vector>

namespace cubeshot {

class World {
public:
    static constexpr PlayerId localPlayerId = 1;

    explicit World(Arena arena = Arena::createDefault());

    void setPlayerInput(PlayerId id, const PlayerInput& input);
    void clearPlayerInput(PlayerId id);
    void requestAction(PlayerId id, bool jump, bool shoot, Vec2 aim);
    void resetPlayers(bool swapSpawns = false);
    void update(float deltaSeconds);

    const Arena& arena() const;
    const std::vector<Player>& players() const;
    const std::vector<Bullet>& bullets() const;

private:
    bool moveBullet(Bullet& bullet, Vec2 destination);
    void updateBullets(float deltaSeconds);

    Arena arena_;
    std::vector<Player> players_;
    std::vector<Bullet> bullets_;
    BulletId nextBulletId_ = 1;
    std::vector<int> pendingDamage_;
};

} // namespace cubeshot
