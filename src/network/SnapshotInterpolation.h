#pragma once

#include "game/Match.h"
#include <chrono>
#include <deque>
#include <utility>

namespace cubeshot::network
{
class SnapshotInterpolation
{
public:
    using Clock = std::chrono::steady_clock;
    void clear();
    void add(const WorldSnapshot& snapshot, Clock::time_point arrival);
    WorldSnapshot sample(const WorldSnapshot& latest, Clock::time_point now) const;

private:
    std::deque<std::pair<Clock::time_point, WorldSnapshot>> history_;
};
} // namespace cubeshot::network
