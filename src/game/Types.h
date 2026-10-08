#pragma once

#include <cstdint>

namespace cubeshot {

using PlayerId = std::uint32_t;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Координаты в пикселях: X направлена вправо, Y — вниз.
struct Rect {
    Vec2 position;
    Vec2 size;
};

} // namespace cubeshot
