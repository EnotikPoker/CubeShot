#pragma once
#include "game/Types.h"

namespace cubeshot
{
struct PlayerInput
{
    float moveDirection = 0;
    bool jump = false, shoot = false;
    Vec2 aimDirection{1, 0};
    bool triggerHeld = false, reload = false, jetpackHeld = false;
    bool activeQ = false, activeE = false;
};
} // namespace cubeshot
