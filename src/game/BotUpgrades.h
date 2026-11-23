#pragma once
#include "game/Match.h"
#include <deque>

namespace cubeshot
{
struct BotRoundStats
{
    float seconds = 0, damage = 0, taken = 0, selfDamage = 0, inReach = 0;
    unsigned shots = 0;
};

UpgradeKind chooseBotUpgrade(const WorldSnapshot& state, PlayerId own,
                             const std::deque<BotRoundStats>& history = {});
} // namespace cubeshot
