#pragma once

#include "game/PlayerInput.h"
#include "game/Types.h"
#include "game/Weapon.h"

namespace cubeshot {

struct Arena;

struct Player {
    static constexpr float defaultSize = 48.0f;
    static constexpr float moveSpeed = 280.0f; // Пикселей в секунду.

    PlayerId id = 0;
    Vec2 position;
    Vec2 velocity;
    Vec2 size{defaultSize, defaultSize};
    int health = 100;
    Weapon weapon;

    Rect bounds() const;
    void applyInput(const PlayerInput& input);
    void update(float deltaSeconds, const Arena& arena);
};

} // namespace cubeshot
