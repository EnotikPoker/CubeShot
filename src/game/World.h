#pragma once

#include "game/Arena.h"
#include "game/Bullet.h"
#include "game/Player.h"

#include <vector>

namespace cubeshot {

class World {
public:
    static constexpr PlayerId localPlayerId = 1;

    World();

    void setPlayerInput(PlayerId id, const PlayerInput& input);
    void update(float deltaSeconds);

    const Arena& arena() const;
    const std::vector<Player>& players() const;
    const std::vector<Bullet>& bullets() const;

private:
    Arena arena_;
    std::vector<Player> players_;
    std::vector<Bullet> bullets_; // Пока пусто: стрельба ещё не реализована.
};

} // namespace cubeshot
