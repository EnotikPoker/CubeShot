#include "game/BodyPhysics.h"
#include "game/Collision.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
namespace
{
bool supported(const Player& top, const Player& bottom)
{
    return top.alive() && bottom.alive() && top.id != bottom.id &&
           std::abs(top.position.y + top.size.y - bottom.position.y) < .1f &&
           top.position.x + top.size.x > bottom.position.x + .1f &&
           top.position.x < bottom.position.x + bottom.size.x - .1f && top.velocity.y >= 0;
}

float shiftX(Player& p, float dx, const Arena& a)
{
    auto old = p.position.x;
    p.position.x = collision::moveHorizontally(p.bounds(), dx, a.bounds, a.platforms);
    return p.position.x - old;
}

float shiftY(Player& p, float dy, const Arena& a)
{
    auto old = p.position.y;
    p.position.y = collision::moveVertically(p.bounds(), dy, a.bounds, a.platforms).y;
    return p.position.y - old;
}
} // namespace

void simulateBodies(std::vector<Player>& players, const Arena& arena, float dt)
{
    std::sort(players.begin(), players.end(), [](const Player& a, const Player& b) { return a.id < b.id; });
    const auto before = players;
    for (auto& p : players)
    {
        bool support = false;
        for (const auto& b : before)
            if (supported(p, b))
                support = true;
        p.update(dt, arena, support);
    }
    // Carry a rider horizontally without transporting it through static geometry.
    for (std::size_t i = 0; i < players.size(); ++i)
        for (std::size_t j = 0; j < players.size(); ++j)
            if (supported(before[i], before[j]))
                shiftX(players[i], players[j].position.x - before[j].position.x, arena);
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        bool changed = false;
        for (std::size_t i = 0; i < players.size(); ++i)
            for (std::size_t j = i + 1; j < players.size(); ++j)
            {
                auto& a = players[i];
                auto& b = players[j];
                if (!a.alive() || !b.alive() || !collision::overlaps(a.bounds(), b.bounds()))
                    continue;
                const float px = std::min(a.position.x + a.size.x, b.position.x + b.size.x) -
                                 std::max(a.position.x, b.position.x);
                const float py = std::min(a.position.y + a.size.y, b.position.y + b.size.y) -
                                 std::max(a.position.y, b.position.y);
                if (px < .0001f || py < .0001f)
                    continue;
                changed = true;
                const bool above = before[i].position.y + before[i].size.y <= before[j].position.y + .1f;
                const bool below = before[j].position.y + before[j].size.y <= before[i].position.y + .1f;
                if (above || below || py < px)
                {
                    Player& top = above ? a : below ? b : a.position.y < b.position.y ? a : b;
                    Player& bottom = top.id == a.id ? b : a;
                    float moved = -shiftY(top, -py, arena);
                    if (moved < py)
                        shiftY(bottom, py - moved, arena);
                    if (top.velocity.y >= bottom.velocity.y)
                    {
                        top.velocity.y = std::max(0.f, bottom.velocity.y);
                        top.grounded = true;
                    }
                    else
                        bottom.velocity.y = std::max(0.f, bottom.velocity.y);
                }
                else
                {
                    const float sign = a.position.x < b.position.x ? -1.f : 1.f;
                    const float first = std::abs(shiftX(a, sign * px * .5f, arena));
                    const float second = std::abs(shiftX(b, -sign * (px - first), arena));
                    if (first + second < px)
                        shiftX(a, sign * (px - first - second), arena);
                }
            }
        if (!changed)
            break;
    }
    for (auto& p : players)
        for (const auto& b : players)
            if (supported(p, b))
                p.grounded = true;
}
} // namespace cubeshot
