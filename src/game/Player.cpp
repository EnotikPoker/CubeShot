#include "game/Player.h"

#include "game/Arena.h"
#include "game/Collision.h"

#include <algorithm>

namespace cubeshot {

Rect Player::bounds() const
{
    return {position, size};
}

void Player::applyInput(const PlayerInput& input)
{
    velocity.x = std::clamp(input.moveDirection, -1.0f, 1.0f) * moveSpeed;
}

void Player::update(float deltaSeconds, const Arena& arena)
{
    if (!(deltaSeconds > 0.0f)) {
        return;
    }

    position.x = collision::moveHorizontally(bounds(), velocity.x * deltaSeconds,
                                            arena.bounds, arena.platforms);
    weapon.update(deltaSeconds);
}

} // namespace cubeshot
