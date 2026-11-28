#pragma once
#include "game/BotController.h"
#include "game/BotUpgrades.h"
#include "game/Match.h"
#include <memory>

namespace cubeshot
{
// Shared autonomous actors for offline and authoritative server matches.
class BotRoster
{
public:
    explicit BotRoster(unsigned seed = 42) : seed_(seed) {}

    void step(Match& match, float dt);
    void reset();

private:
    struct Actor
    {
        std::unique_ptr<BotController> controller;
        std::uint64_t sequence = 0;
        unsigned round = ~0u, life = ~0u, difficulty = 1;
        bool held = false;
        PlayerId target = 0;
        float targetTime = 0;
        std::deque<BotRoundStats> history;
        BotRoundStats stats;
        std::array<int, maxPlayers> health{};
        unsigned shots = 0;
        bool observed = false, finishedHistory = false;
    };

    std::array<Actor, maxPlayers> actors_;
    unsigned seed_;
};
} // namespace cubeshot
