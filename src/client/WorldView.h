#pragma once
#include "game/Types.h"
#include <algorithm>

namespace cubeshot::client
{
struct WorldView
{
    float scale = 1;
    Vec2 origin{}, offset{};

    explicit WorldView(Rect bounds)
    {
        scale = std::min(960.f / bounds.size.x, 540.f / bounds.size.y);
        origin = bounds.position;
        offset = {(960 - bounds.size.x * scale) / 2, (540 - bounds.size.y * scale) / 2};
    }

    Vec2 toScreen(Vec2 p) const
    {
        return {offset.x + (p.x - origin.x) * scale, offset.y + (p.y - origin.y) * scale};
    }

    Vec2 toWorld(Vec2 p) const
    {
        return {origin.x + (p.x - offset.x) / scale, origin.y + (p.y - offset.y) / scale};
    }
};
} // namespace cubeshot::client
