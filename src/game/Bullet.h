#pragma once

#include "game/Types.h"

namespace cubeshot {

// Формат состояния будущей пули. Создание и симуляция пока не реализованы.
struct Bullet {
    Vec2 position;
    Vec2 velocity;
    int damage = 10;
    PlayerId shooterId = 0;
};

} // namespace cubeshot
