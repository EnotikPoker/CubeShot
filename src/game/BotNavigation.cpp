#include "game/BotNavigation.h"
#include "game/Collision.h"
#include "game/Player.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <queue>

namespace cubeshot
{
namespace
{
constexpr float step = 1.f / 120;

float distance(Vec2 a, Vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}
} // namespace

BotNavigation::BotNavigation(const Arena& arena)
{
    for (std::size_t surface = 0; surface < arena.platforms.size(); ++surface)
    {
        const auto& r = arena.platforms[surface];
        const float left = std::max(r.position.x - Player::defaultSize + 8, arena.bounds.position.x);
        const float right = std::min(r.position.x + r.size.x - 8,
                                     arena.bounds.position.x + arena.bounds.size.x - Player::defaultSize);
        std::vector<float> samples;
        for (float x = left; x <= right; x += 32)
            samples.push_back(x);
        if (right >= left)
            samples.push_back(right);
        for (const auto& spawn : arena.spawnPoints)
            if (std::abs(spawn.y + Player::defaultSize - r.position.y) < .1f && spawn.x >= left &&
                spawn.x <= right)
                samples.push_back(spawn.x);
        std::sort(samples.begin(), samples.end());
        samples.erase(
            std::unique(samples.begin(), samples.end(), [](float a, float b) { return std::abs(a - b) < 1; }),
            samples.end());
        for (float x : samples)
        {
            Rect body{{x, r.position.y - Player::defaultSize}, {Player::defaultSize, Player::defaultSize}};
            if (body.position.y < arena.bounds.position.y)
                continue;
            if (std::none_of(arena.platforms.begin(), arena.platforms.end(),
                             [&](const Rect& solid) { return collision::overlaps(body, solid); }))
                nodes_.push_back({body.position, surface});
        }
    }
    outgoing_.resize(nodes_.size());
    auto add = [&](BotTraversal edge)
    {
        for (auto index : outgoing_[edge.from])
        {
            auto& old = edges_[index];
            if (old.to == edge.to && old.jump == edge.jump)
            {
                if (edge.durationTicks < old.durationTicks)
                    old = edge;
                return;
            }
        }
        outgoing_[edge.from].push_back(edges_.size());
        edges_.push_back(edge);
    };
    for (std::size_t from = 0; from < nodes_.size(); ++from)
    {
        // Consecutive samples on the same surface form walkable edges.
        for (int offset : {-1, 1})
        {
            const auto to = static_cast<int>(from) + offset;
            if (to < 0 || to >= static_cast<int>(nodes_.size()) || nodes_[from].surface != nodes_[to].surface)
                continue;
            Player p;
            p.position = nodes_[from].position;
            const float dx = nodes_[to].position.x - p.position.x;
            const float end = collision::moveHorizontally(p.bounds(), dx, arena.bounds, arena.platforms);
            if (std::abs(end - nodes_[to].position.x) < .1f)
                add({from, static_cast<std::size_t>(to), false, dx > 0 ? 1 : -1, 0,
                     std::max(1, static_cast<int>(std::ceil(std::abs(dx) / Player::moveSpeed / step)))});
        }
        for (bool jump : {true, false})
            for (int direction : {-1, 0, 1})
                for (int hold : {18, 36, 60, 90, 120})
                {
                    if ((!jump && direction == 0) || (direction == 0 && hold != 18))
                        continue;
                    Player p;
                    p.position = nodes_[from].position;
                    bool airborne = false;
                    for (int tick = 0; tick < 180; ++tick)
                    {
                        p.applyInput({tick < hold ? static_cast<float>(direction) : 0.f, jump && tick == 0});
                        p.update(step, arena);
                        airborne = airborne || !p.grounded;
                        if (!airborne || !p.grounded)
                            continue;
                        auto to = nearest(p.position);
                        if (std::abs(nodes_[to].position.y - p.position.y) < .1f &&
                            std::abs(nodes_[to].position.x - p.position.x) <= 17 && to != from)
                        {
                            // A jump must solve an obstacle/height change, not exploit rounded
                            // landing samples to appear faster than walking on the same surface.
                            const float dx = nodes_[to].position.x - nodes_[from].position.x;
                            const Rect body{nodes_[from].position,
                                            {Player::defaultSize, Player::defaultSize}};
                            const bool walkable = nodes_[from].surface == nodes_[to].surface &&
                                                  std::abs(collision::moveHorizontally(body, dx, arena.bounds,
                                                                                       arena.platforms) -
                                                           nodes_[to].position.x) < .1f;
                            if (!jump || !walkable)
                                add({from, to, jump, direction, hold, tick + 1});
                        }
                        break;
                    }
                }
    }
}

std::shared_ptr<const BotNavigation> BotNavigation::forMap(MapId map)
{
    // Client/game simulation is single-threaded. Cache lives for the application's lifetime.
    static std::map<MapId, std::shared_ptr<const BotNavigation>> cache;
    auto& graph = cache[map];
    if (!graph)
        graph = std::make_shared<const BotNavigation>(Arena::create(map, map == MapId::Test));
    return graph;
}

std::shared_ptr<const BotNavigation> BotNavigation::forArena(const Arena& arena)
{
    const auto original = Arena::create(arena.map, arena.map == MapId::Test);
    auto same = [](const Rect& a, const Rect& b)
    {
        return a.position.x == b.position.x && a.position.y == b.position.y && a.size.x == b.size.x &&
               a.size.y == b.size.y;
    };
    if (same(arena.bounds, original.bounds) && arena.platforms.size() == original.platforms.size() &&
        std::equal(arena.platforms.begin(), arena.platforms.end(), original.platforms.begin(), same))
        return forMap(arena.map);
    // Custom geometry must never silently use the stock map's routes.
    return std::make_shared<const BotNavigation>(arena);
}

const std::vector<BotWaypoint>& BotNavigation::nodes() const
{
    return nodes_;
}

const std::vector<BotTraversal>& BotNavigation::edges() const
{
    return edges_;
}

std::size_t BotNavigation::nearest(Vec2 at) const
{
    std::size_t best = 0;
    float cost = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < nodes_.size(); ++i)
    {
        float next = distance(at, nodes_[i].position);
        if (next < cost)
        {
            best = i;
            cost = next;
        }
    }
    return best;
}

std::vector<BotTraversal> BotNavigation::route(Vec2 from, Vec2 goal, int avoidFrom, int avoidTo) const
{
    if (nodes_.empty())
        return {};
    const auto start = nearest(from), finish = nearest(goal);
    std::vector<float> costs(nodes_.size(), std::numeric_limits<float>::max());
    std::vector<int> previous(nodes_.size(), -1);
    using Item = std::pair<float, std::size_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
    open.push({0, start});
    costs[start] = 0;
    while (!open.empty())
    {
        auto [cost, at] = open.top();
        open.pop();
        if (at == finish)
            break;
        if (cost > costs[at])
            continue;
        for (auto index : outgoing_[at])
        {
            const auto& edge = edges_[index];
            if (static_cast<int>(edge.from) == avoidFrom && static_cast<int>(edge.to) == avoidTo)
                continue;
            const float correction = std::abs(nodes_[edge.to].position.x - nodes_[edge.from].position.x -
                                              edge.direction * Player::moveSpeed * step *
                                                  std::min(edge.holdTicks, edge.durationTicks));
            const bool airborne = edge.jump || nodes_[edge.from].surface != nodes_[edge.to].surface;
            const float next =
                cost + edge.durationTicks + (airborne ? 12 + correction / Player::moveSpeed / step : 0);
            if (next < costs[edge.to])
            {
                costs[edge.to] = next;
                previous[edge.to] = static_cast<int>(index);
                open.push({next, edge.to});
            }
        }
    }
    std::vector<BotTraversal> path;
    for (auto at = finish; at != start;)
    {
        if (previous[at] < 0)
            return {};
        const auto& edge = edges_[previous[at]];
        path.push_back(edge);
        at = edge.from;
    }
    std::reverse(path.begin(), path.end());
    return path;
}
} // namespace cubeshot
