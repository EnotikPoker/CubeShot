#pragma once
#include "game/World.h"
#include <array>
#include <string>

namespace cubeshot
{
enum class MatchPhase
{
    Lobby,
    Countdown,
    Round,
    RoundEnd,
    Finished
};
enum class AbortReason
{
    None,
    PeerLeft,
    PeerTimeout
};

struct WorldSnapshot
{
    std::uint64_t tick = 0;
    std::uint32_t round = 0;
    MatchPhase phase = MatchPhase::Lobby;
    float remaining = 0;
    std::array<unsigned, 2> score{};
    std::array<bool, 2> connected{}, ready{};
    PlayerId winner = 0;
    AbortReason aborted = AbortReason::None;
    std::vector<Player> players;
    std::vector<Bullet> bullets;
    std::array<std::uint64_t, 2> inputAck{}, actionAck{};
    std::array<std::string, 2> names{"Игрок 1", "Игрок 2"};
    MapId map = MapId::Duel;
    bool voting = false;
    std::array<int, 2> votes{{-1, -1}};
    std::uint64_t selectionRevision = 0, stateSequence = 0;
};

// Чистые правила дуэли. Сеть назначает слоты; только сервер меняет Match.
class Match
{
public:
    static constexpr unsigned winsRequired = 5;

    explicit Match(bool voting = false) : voting_(voting) {}

    bool selectMap(PlayerId id, MapId map, std::uint64_t revision);
    bool changeMap(PlayerId id, std::uint64_t revision);
    std::vector<GameEvent> takeEvents();
    void connect(PlayerId id);
    void disconnect(PlayerId id, AbortReason reason = AbortReason::PeerLeft);
    void setReady(PlayerId id, bool ready);
    void setInput(PlayerId id, const PlayerInput& input);
    bool action(PlayerId id, std::uint32_t round, std::uint64_t sequence, bool jump, bool shoot, Vec2 aim);
    void update(float dt);
    WorldSnapshot snapshot() const;

    const World& world() const
    {
        return world_;
    }

    MatchPhase phase() const
    {
        return phase_;
    }

    std::uint32_t round() const
    {
        return round_;
    }

private:
    void beginCountdown();
    World world_{Arena::createDuel(), 2};
    bool voting_ = false, rematch_ = false;
    std::array<int, 2> votes_{{-1, -1}};
    std::uint64_t selectionRevision_ = 0;
    std::vector<GameEvent> events_;
    MatchPhase phase_ = MatchPhase::Lobby;
    AbortReason aborted_ = AbortReason::None;
    std::array<bool, 2> connected_{}, ready_{};
    std::array<unsigned, 2> score_{};
    std::array<std::uint64_t, 2> lastAction_{};
    std::uint64_t tick_ = 0;
    std::uint32_t round_ = 0;
    unsigned roundsPlayed_ = 0;
    float remaining_ = 0;
    PlayerId winner_ = 0;
};
} // namespace cubeshot
