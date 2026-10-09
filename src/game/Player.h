#pragma once

#include "game/PlayerInput.h"
#include "game/Types.h"
#include "game/Weapon.h"

namespace cubeshot {

struct Arena;

struct Player {
    static constexpr float defaultSize = 48.0f;
    static constexpr float moveSpeed = 280.0f; // Пикселей в секунду.
    static constexpr float jumpSpeed = 620.0f; // Начальная скорость вверх, пикселей/с.
    static constexpr float gravity = 1800.0f; // Пикселей/с².

    PlayerId id = 0;
    Vec2 position;
    Vec2 velocity;
    Vec2 size{defaultSize, defaultSize};
    int health = 100;
    Weapon weapon;
    bool grounded = false;

    Rect bounds() const;
    void applyInput(const PlayerInput& input);
    void update(float deltaSeconds, const Arena& arena);

private:
    bool jumpHeld_ = false;
    bool jumpRequested_ = false;
};

} // namespace cubeshot
