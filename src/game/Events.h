#pragma once
#include "game/Types.h"

namespace cubeshot
{
enum class EventKind
{
    Shot,
    Hit,
    Death,
    SurfaceImpact
};

struct GameEvent
{
    std::uint64_t id = 0, tick = 0, shotSequence = 0;
    std::uint32_t round = 0;
    EventKind kind = EventKind::Shot;
    PlayerId actor = 0, target = 0;
    Vec2 position, direction;
};
} // namespace cubeshot
