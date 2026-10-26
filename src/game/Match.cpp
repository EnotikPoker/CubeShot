#include "game/Match.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace cubeshot
{
namespace
{
bool validId(PlayerId id)
{
    return id == 1 || id == 2;
}
} // namespace

bool Match::selectMap(PlayerId id, MapId map, std::uint64_t revision)
{
    if (!validId(id) || !connected_[id - 1] || phase_ != MatchPhase::Lobby ||
        revision != selectionRevision_ || !validMap(map))
        return false;
    if (!voting_ && id != 1)
        return false;
    if (voting_)
        votes_[id - 1] = static_cast<int>(map);
    else
        world_ = World{Arena::create(map), 2};
    ready_ = {};
    ++selectionRevision_;
    return true;
}

bool Match::changeMap(PlayerId id, std::uint64_t revision)
{
    if (!validId(id) || !connected_[id - 1] || phase_ != MatchPhase::Finished ||
        revision != selectionRevision_ || (!voting_ && id != 1))
        return false;
    phase_ = MatchPhase::Lobby;
    score_ = {};
    ready_ = {};
    votes_ = {{-1, -1}};
    rematch_ = false;
    roundsPlayed_ = 0;
    winner_ = 0;
    ++round_;
    ++selectionRevision_;
    world_.resetPlayers();
    events_.clear();
    return true;
}

std::vector<GameEvent> Match::takeEvents()
{
    auto out = std::move(events_);
    events_.clear();
    return out;
}

void Match::connect(PlayerId id)
{
    if (!validId(id))
        return;
    connected_[id - 1] = true;
    ready_[id - 1] = false;
}

void Match::disconnect(PlayerId id, AbortReason reason)
{
    if (!validId(id) || !connected_[id - 1])
        return;
    connected_[id - 1] = false;
    ready_ = {};
    score_ = {};
    lastAction_ = {};
    aborted_ = reason;
    phase_ = MatchPhase::Lobby;
    remaining_ = 0;
    winner_ = 0;
    roundsPlayed_ = 0;
    votes_ = {{-1, -1}};
    rematch_ = false;
    events_.clear();
    ++selectionRevision_;
    ++round_; // Любые пакеты прерванного раунда больше не действуют.
    world_.resetPlayers();
}

void Match::beginCountdown()
{
    if (phase_ == MatchPhase::Finished)
    {
        score_ = {};
        roundsPlayed_ = 0;
    }
    if (voting_ && phase_ == MatchPhase::Lobby && !rematch_)
    {
        static std::mt19937 rng(std::random_device{}());
        const auto pick =
            votes_[0] == votes_[1] ? votes_[0] : votes_[std::uniform_int_distribution<int>(0, 1)(rng)];
        world_ = World{Arena::create(static_cast<MapId>(pick)), 2};
    }
    rematch_ = true;
    ++selectionRevision_;
    ++round_;
    lastAction_ = {};
    aborted_ = AbortReason::None;
    winner_ = 0;
    world_.resetPlayers(roundsPlayed_ % 2 != 0);
    phase_ = MatchPhase::Countdown;
    remaining_ = 3;
}

void Match::setReady(PlayerId id, bool ready)
{
    if (!validId(id) || !connected_[id - 1] ||
        (phase_ != MatchPhase::Lobby && phase_ != MatchPhase::Finished))
        return;
    if (ready && voting_ && phase_ == MatchPhase::Lobby && (votes_[0] < 0 || votes_[1] < 0))
        return;
    ready_[id - 1] = ready;
    if (connected_[0] && connected_[1] && ready_[0] && ready_[1])
        beginCountdown();
}

void Match::setInput(PlayerId id, const PlayerInput& input)
{
    if (!validId(id) || !connected_[id - 1] || phase_ != MatchPhase::Round)
        return;
    if (!std::isfinite(input.moveDirection) || !std::isfinite(input.aimDirection.x) ||
        !std::isfinite(input.aimDirection.y))
        return;
    PlayerInput continuous = input;
    continuous.jump = continuous.shoot = false;
    world_.setPlayerInput(id, continuous);
}

void Match::clearInput(PlayerId id)
{
    if (validId(id))
        world_.clearPlayerInput(id);
}

bool Match::action(PlayerId id, std::uint32_t round, std::uint64_t sequence, bool jump, bool shoot, Vec2 aim)
{
    if (!validId(id) || !connected_[id - 1] || phase_ != MatchPhase::Round || round != round_ ||
        sequence == 0 || sequence <= lastAction_[id - 1] || !std::isfinite(aim.x) || !std::isfinite(aim.y) ||
        std::hypot(aim.x, aim.y) < 0.0001f)
        return false;
    lastAction_[id - 1] = sequence;
    world_.requestAction(id, jump, shoot, aim, sequence);
    return true;
}

void Match::update(float dt)
{
    if (!std::isfinite(dt) || dt <= 0)
        return;
    ++tick_;
    if (phase_ == MatchPhase::Countdown || phase_ == MatchPhase::RoundEnd)
    {
        remaining_ = std::max(0.0f, remaining_ - dt);
        if (remaining_ <= 0.0001f)
        {
            remaining_ = 0;
            if (phase_ == MatchPhase::RoundEnd)
            {
                beginCountdown();
                phase_ = MatchPhase::Round;
                remaining_ = 0;
                ready_ = {};
            }
            else
            {
                phase_ = MatchPhase::Round;
                ready_ = {};
            }
        }
    }
    else if (phase_ == MatchPhase::Round)
    {
        world_.update(dt);
        for (auto event : world_.takeEvents())
        {
            event.tick = tick_;
            event.round = round_;
            events_.push_back(event);
        }
        if (events_.size() > 256)
            events_.erase(events_.begin(), events_.end() - 256);
        const bool first = world_.players()[0].alive(), second = world_.players()[1].alive();
        if (!first || !second)
        {
            winner_ = first == second ? 0 : first ? 1 : 2;
            if (winner_)
                ++score_[winner_ - 1];
            ++roundsPlayed_;
            for (PlayerId id : {1u, 2u})
                world_.clearPlayerInput(id);
            phase_ =
                winner_ && score_[winner_ - 1] >= winsRequired ? MatchPhase::Finished : MatchPhase::RoundEnd;
            remaining_ = phase_ == MatchPhase::RoundEnd ? 3.0f : 0;
            ready_ = {};
        }
    }
}

WorldSnapshot Match::snapshot() const
{
    WorldSnapshot s{tick_,  round_,  phase_,   remaining_,       score_,          connected_,
                    ready_, winner_, aborted_, world_.players(), world_.bullets()};
    s.map = world_.arena().map;
    s.voting = voting_;
    s.votes = votes_;
    s.selectionRevision = selectionRevision_;
    return s;
}
} // namespace cubeshot
