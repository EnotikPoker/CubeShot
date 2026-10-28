#pragma once
#include "game/Match.h"
#include <deque>
#include <set>

namespace cubeshot
{
struct PredictedCommand
{
    std::uint64_t sequence = 0;
    PlayerInput input;
};

// Local presentation only. Never changes authoritative HP, bullets or score.
class Prediction
{
public:
    static constexpr float step = 1.0f / 120;
    void reset();
    void reconcile(const WorldSnapshot& snapshot, PlayerId own);
    void advance(PlayerInput input);
    std::vector<GameEvent> takeEvents();
    std::optional<GameEvent> action(std::uint64_t sequence, bool jump, bool shoot, Vec2 aim);

    const std::optional<Player>& player() const
    {
        return player_;
    }

    const std::deque<PredictedCommand>& commands() const
    {
        return commands_;
    }

    const std::vector<Bullet>& bullets() const
    {
        return bullets_;
    }

    std::uint64_t sequence() const
    {
        return sequence_;
    }

    Vec2 renderOffset() const
    {
        return offset_;
    }

private:
    struct Action
    {
        std::uint64_t sequence, input;
        bool jump, shoot;
        Vec2 aim;
    };

    void simulate(const PredictedCommand& command, bool visuals = false);
    std::optional<GameEvent> presentShot(const Bullet& bullet);
    std::vector<GameEvent> events_;
    std::set<std::uint32_t> shown_;
    Arena arena_ = Arena::createDuel();
    std::vector<Player> others_;
    float visualCooldown_ = 0;
    std::optional<Player> player_;
    std::deque<PredictedCommand> commands_;
    std::deque<Action> actions_;
    std::vector<Bullet> bullets_;
    std::uint64_t sequence_ = 0, actionAck_ = 0;
    std::uint32_t round_ = 0;
    MatchPhase phase_ = MatchPhase::Lobby;
    Vec2 offset_{};
    bool waitingForSnapshot_ = false;
};
} // namespace cubeshot
