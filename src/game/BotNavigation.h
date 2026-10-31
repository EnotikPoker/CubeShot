#pragma once
#include "game/Arena.h"
#include <memory>
#include <vector>

namespace cubeshot
{
struct BotWaypoint
{
    Vec2 position;
    std::size_t surface;
};

struct BotTraversal
{
    std::size_t from, to;
    bool jump;
    int direction, holdTicks, durationTicks;
};

// Static geometry only. Every airborne edge is verified with Player's real physics.
class BotNavigation
{
public:
    explicit BotNavigation(const Arena& arena);
    static std::shared_ptr<const BotNavigation> forMap(MapId map);
    static std::shared_ptr<const BotNavigation> forArena(const Arena& arena);
    const std::vector<BotWaypoint>& nodes() const;
    const std::vector<BotTraversal>& edges() const;
    std::vector<BotTraversal> route(Vec2 from, Vec2 goal, int avoidFrom = -1, int avoidTo = -1,
                                    const std::vector<float>& hazards = {}) const;
    std::size_t nearest(Vec2 at) const;

private:
    std::vector<BotWaypoint> nodes_;
    std::vector<BotTraversal> edges_;
    std::vector<std::vector<std::size_t>> outgoing_;
};
} // namespace cubeshot
