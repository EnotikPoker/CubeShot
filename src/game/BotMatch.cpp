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

BotMatch::BotMatch(MapId map, BotDifficulty difficulty, std::string nickname, std::uint32_t seed,
                   MatchRules rules, std::array<unsigned, maxPlayers> difficulties,
                   std::array<TeamId, maxPlayers> teams)
    : match_(false, seed), roster_(seed), nickname_(std::move(nickname))
{
    if (!validMap(map) || !rules.valid())
        throw std::invalid_argument("Invalid bot match setup");
    match_.connect(1);
    match_.selectRules(1, rules, match_.snapshot().selectionRevision);
    match_.selectMap(1, map, match_.snapshot().selectionRevision);
    match_.selectTeam(1, teams[0], match_.snapshot().selectionRevision);
    for (unsigned i = 1; i < rules.participants; ++i)
        match_.configureSlot(1, i + 1, SlotKind::Bot,
                             difficulties[i] <= 2 ? difficulties[i] : unsigned(difficulty), teams[i],
                             match_.snapshot().selectionRevision);
    rematch();
}

void BotMatch::control(const PlayerInput& input)
{
    match_.setInput(1, input);
}

void BotMatch::action(bool jump, bool shoot, Vec2 aim, bool reload, unsigned activeMask)
{
    match_.action(1, match_.round(), ++humanSequence_, jump, shoot, aim, reload, activeMask);
}

bool BotMatch::chooseWeapon(bool reroll)
{
    return match_.chooseWeapon(1, reroll, match_.round(), match_.snapshot().selectionRevision);
}

bool BotMatch::chooseUpgrade(UpgradeKind kind)
{
    return match_.chooseUpgrade(1, kind, match_.round(), match_.snapshot().selectionRevision);
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
        accumulator_ = std::max(0.0, accumulator_ - step);
        roster_.step(match_, float(step));
        match_.update(float(step));
    }
}

void BotMatch::rematch(const std::string& nickname)
{
    if (!nickname.empty())
        nickname_ = nickname;
    accumulator_ = 0;
    roster_.reset();
    match_.setReady(1, true);
}

WorldSnapshot BotMatch::snapshot() const
{
    auto result = match_.snapshot();
    result.names[0] = nickname_;
    for (unsigned i = 1; i < result.rules.participants; ++i)
        result.names[i] = i == 1 ? "Бот" : "Бот " + std::to_string(i + 1);
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
