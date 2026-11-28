#pragma once
#include "game/BotRoster.h"
#include "game/BotUpgrades.h"
#include "game/Match.h"
#include <random>
#include <string>

namespace cubeshot
{
class BotMatch
{
public:
    BotMatch(MapId map, BotDifficulty difficulty, std::string nickname, std::uint32_t seed,
             MatchRules rules = {}, std::array<unsigned, maxPlayers> difficulties = {{~0u, ~0u, ~0u, ~0u}},
             std::array<TeamId, maxPlayers> teams = {{1, 2, 1, 2}});
    void control(const PlayerInput& input);
    void action(bool jump, bool shoot, Vec2 aim, bool reload = false, unsigned activeMask = 0);
    void clearInput();
    bool chooseWeapon(bool reroll);
    bool chooseUpgrade(UpgradeKind kind);
    void update(float dt);
    void rematch(const std::string& nickname = {});
    WorldSnapshot snapshot() const;
    std::vector<GameEvent> takeEvents();
    const Match& match() const;

private:
    Match match_;
    BotRoster roster_;
    std::string nickname_;
    std::uint64_t humanSequence_ = 0;
    double accumulator_ = 0;
};
} // namespace cubeshot
