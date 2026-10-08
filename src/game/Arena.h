#pragma once

#include "game/Types.h"

#include <vector>

namespace cubeshot {

struct Arena {
    Rect bounds;
    std::vector<Rect> platforms;
    std::vector<Vec2> spawnPoints; // Положение верхнего левого угла персонажа.

    static Arena createDefault();
};

} // namespace cubeshot
