#include "network/SnapshotInterpolation.h"
#include <algorithm>

namespace cubeshot::network
{
void SnapshotInterpolation::clear()
{
    history_.clear();
}

void SnapshotInterpolation::add(const WorldSnapshot& snapshot, Clock::time_point arrival)
{
    history_.emplace_back(arrival, snapshot);
    while (history_.size() > 12)
        history_.pop_front();
}

WorldSnapshot SnapshotInterpolation::sample(const WorldSnapshot& latest, Clock::time_point now) const
{
    auto out = latest;
    const auto target = now - std::chrono::milliseconds(33);
    for (std::size_t i = 1; i < history_.size(); ++i)
    {
        const auto& a = history_[i - 1];
        const auto& b = history_[i];
        if (target < a.first || target > b.first || a.second.map != b.second.map || a.second.map != out.map ||
            a.second.round != b.second.round || a.second.round != out.round || a.second.phase != out.phase ||
            b.second.phase != out.phase)
            continue;
        const float t = std::chrono::duration<float>(target - a.first).count() /
                        std::chrono::duration<float>(b.first - a.first).count();
        auto lerp = [t](Vec2 x, Vec2 y) { return Vec2{x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t}; };
        for (auto& p : out.players)
        {
            auto from = std::find_if(a.second.players.begin(), a.second.players.end(),
                                     [&](const auto& v) { return v.id == p.id; });
            auto to = std::find_if(b.second.players.begin(), b.second.players.end(),
                                   [&](const auto& v) { return v.id == p.id; });
            if (from != a.second.players.end() && to != b.second.players.end() && p.alive())
                p.position = lerp(from->position, to->position);
        }
        for (auto& bullet : out.bullets)
        {
            auto from = std::find_if(a.second.bullets.begin(), a.second.bullets.end(),
                                     [&](const auto& v) { return v.id == bullet.id; });
            auto to = std::find_if(b.second.bullets.begin(), b.second.bullets.end(),
                                   [&](const auto& v) { return v.id == bullet.id; });
            if (from != a.second.bullets.end() && to != b.second.bullets.end())
                bullet.position = lerp(from->position, to->position);
        }
        break;
    }
    return out;
}
} // namespace cubeshot::network
