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
                   MatchRules rules)
    : upgradeRandom_(seed ^ 0x53a7e1u), match_(false, seed), bot_(botArena(map), difficulty, seed),
      nickname_(std::move(nickname))
{
    match_.connect(1);
    match_.connect(2);
    match_.selectMap(1, map, match_.snapshot().selectionRevision);
    match_.selectRules(1, rules, match_.snapshot().selectionRevision);
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
        const float delta = static_cast<float>(step);
        accumulator_ = std::max(0.0, accumulator_ - step);
        if (round_ != match_.round() || life_ != match_.world().players()[1].lifeRevision)
        {
            round_ = match_.round();
            life_ = match_.world().players()[1].lifeRevision;
            bot_.reset();
            bot_.prepare(match_.world().players()[1].abilities, &match_.world().arena());
            botHeld_ = false;
        }
        if (match_.phase() == MatchPhase::Round)
        {
            auto observation = observeBot(match_.world(), 2, 1);
            auto state = match_.snapshot();
            if (state.rules.mode == GameMode::Control)
                observation.objective = match_.world().arena().controlAnchors()[state.mode.zone];
            const auto input = bot_.update(observation, delta);
            match_.setInput(2, input);
            if (input.reload)
                match_.action(2, match_.round(), ++botSequence_, false, false, input.aimDirection, true);
            unsigned activeMask = (input.activeQ ? 1 : 0) | (input.activeE ? 2 : 0);
            if (input.jump || (input.shoot && !botHeld_) || activeMask)
                match_.action(2, match_.round(), ++botSequence_, input.jump, input.shoot && !botHeld_,
                              input.aimDirection, false, activeMask);
            botHeld_ = input.shoot;
        }
        if (match_.phase() == MatchPhase::Armory || match_.snapshot().mode.respawn[1].choosing)
        {
            auto state = match_.snapshot();
            if (state.weaponChoice[1] == WeaponChoice::Pending)
            {
                auto kind = chooseBotUpgrade(state, 2, history_);
                if (!match_.chooseUpgrade(2, kind, state.round, state.selectionRevision))
                    match_.chooseWeapon(2, false, state.round, state.selectionRevision);
            }
        }
        auto beforePhase = match_.phase();
        auto before = match_.world().players();
        if (beforePhase == MatchPhase::Round)
        {
            roundStats_.seconds += delta;
            auto a = before[1].weaponOrigin(), b = before[0].weaponOrigin();
            if (std::hypot(a.x - b.x, a.y - b.y) <
                botWeaponProfile(before[1].weapon.kind).maximum * before[1].abilities.range())
                roundStats_.inReach += delta;
        }
        match_.update(delta);
        if (beforePhase == MatchPhase::Round)
        {
            auto after = match_.world().players();
            roundStats_.damage += std::max(0, before[0].health - after[0].health);
            auto lost = std::max(0, before[1].health - after[1].health);
            roundStats_.taken += lost;
            if (after[1].lastDamageActor == 2)
                roundStats_.selfDamage += lost;
            roundStats_.shots += after[1].weapon.shots - before[1].weapon.shots;
            if (match_.phase() != MatchPhase::Round || (before[1].alive() && !after[1].alive()))
            {
                history_.push_back(roundStats_);
                if (history_.size() > 3)
                    history_.pop_front();
                roundStats_ = {};
            }
        }
    }
}

void BotMatch::rematch(const std::string& nickname)
{
    if (!nickname.empty())
        nickname_ = nickname;
    accumulator_ = 0;
    history_.clear();
    roundStats_ = {};
    match_.setReady(1, true);
    match_.setReady(2, true);
    bot_.reset();
    botHeld_ = false;
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
