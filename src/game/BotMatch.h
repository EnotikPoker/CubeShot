#pragma once
#include "game/BotController.h"
#include "game/Match.h"
#include <string>

namespace cubeshot
{
class BotMatch
{
public:
    BotMatch(MapId map, BotDifficulty difficulty, std::string nickname, std::uint32_t seed);
    void control(const PlayerInput& input);
    void action(bool jump, bool shoot, Vec2 aim);
    void clearInput();
    void update(float dt);
    void rematch(const std::string& nickname = {});
    WorldSnapshot snapshot() const;
    std::vector<GameEvent> takeEvents();
    const Match& match() const;

private:
    Match match_;
    BotController bot_;
    std::string nickname_;
    std::uint64_t humanSequence_ = 0, botSequence_ = 0;
    std::uint32_t round_ = 0;
    double accumulator_ = 0;
};
} // namespace cubeshot
