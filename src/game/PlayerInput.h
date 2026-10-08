#pragma once

#include "game/Types.h"

namespace cubeshot {

// Действия игрока без привязки к SDL или конкретным клавишам.
struct PlayerInput {
    float moveDirection = 0.0f; // От -1 (влево) до 1 (вправо).
    // Зарезервированы для будущих механик; сейчас не обрабатываются.
    bool jump = false;
    bool shoot = false;
    Vec2 aimDirection{1.0f, 0.0f};
};

} // namespace cubeshot
