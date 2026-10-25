#include "game/BotMatch.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubeshot
{
namespace
{
Arena botArena(MapId map)
{
    if (!validMap(map))
        throw std::invalid_argument("Bot match requires a duel map");
    return Arena::create(map);
}
} // namespace

BotMatch::BotMatch(MapId map, BotDifficulty difficulty, std::string nickname, std::uint32_t seed)
    : bot_(botArena(map), difficulty, seed), nickname_(std::move(nickname))
{
    match_.connect(1);
    match_.connect(2);
    match_.selectMap(1, map, match_.snapshot().selectionRevision);
    rematch();
}

void BotMatch::control(const PlayerInput& input)
{
    match_.setInput(1, input);
}

void BotMatch::action(bool jump, bool shoot, Vec2 aim)
{
    match_.action(1, match_.round(), ++humanSequence_, jump, shoot, aim);
}

void BotMatch::clearInput()
{
    match_.clearInput(1);
}

void BotMatch::update(float dt)
{
    if (!(dt > 0) || !std::isfinite(dt))
        return;
    accumulator_ += dt;
    constexpr double step = 1.0 / 120;
    while (accumulator_ + 1e-9 >= step)
    {
        const float delta = static_cast<float>(step);
        accumulator_ = std::max(0.0, accumulator_ - step);
        if (round_ != match_.round())
        {
            round_ = match_.round();
            bot_.reset();
        }
        if (match_.phase() == MatchPhase::Round)
        {
            const auto input = bot_.update(observeBot(match_.world(), 2, 1), delta);
            match_.setInput(2, input);
            if (input.jump || input.shoot)
                match_.action(2, match_.round(), ++botSequence_, input.jump, input.shoot, input.aimDirection);
        }
        match_.update(delta);
    }
}

void BotMatch::rematch(const std::string& nickname)
{
    if (!nickname.empty())
        nickname_ = nickname;
    accumulator_ = 0;
    match_.setReady(1, true);
    match_.setReady(2, true);
    bot_.reset();
    round_ = match_.round();
}

WorldSnapshot BotMatch::snapshot() const
{
    auto result = match_.snapshot();
    result.names = {nickname_, "Бот"};
    return result;
}

std::vector<GameEvent> BotMatch::takeEvents()
{
    return match_.takeEvents();
}

const Match& BotMatch::match() const
{
    return match_;
}
} // namespace cubeshot
