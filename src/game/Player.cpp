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
    jumpRequested_ = jumpRequested_ || (input.jump && !jumpHeld_);
    jumpHeld_ = input.jump;
}

void Player::update(float deltaSeconds, const Arena& arena)
{
    if (!(deltaSeconds > 0.0f)) {
        return;
    }

    grounded = collision::isSupported(bounds(), arena.bounds, arena.platforms);
    if (jumpRequested_ && grounded) {
        velocity.y = -jumpSpeed;
        grounded = false;
    }
    jumpRequested_ = false;

    // Короткие шаги сохраняют дугу прыжка и столкновения при редких обновлениях.
    double remaining = deltaSeconds;
    constexpr double maxStep = 1.0 / 120.0;
    while (remaining > 0.0) {
        const float step = static_cast<float>(std::min(remaining, maxStep));
        remaining -= std::min(remaining, maxStep);
        position.x = collision::moveHorizontally(bounds(), velocity.x * step,
                                                arena.bounds, arena.platforms);
        velocity.y += gravity * step;
        const auto movement = collision::moveVertically(bounds(), velocity.y * step,
                                                        arena.bounds, arena.platforms);
        position.y = movement.y;
        grounded = movement.blocked && velocity.y > 0.0f;
        if (movement.blocked) {
            velocity.y = 0.0f;
        }
    }
    weapon.update(deltaSeconds);
}

} // namespace cubeshot
