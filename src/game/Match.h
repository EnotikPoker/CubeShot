#pragma once
#include "game/Modes.h"
#include "game/World.h"
#include <array>
#include <random>
#include <string>

namespace cubeshot
{
enum class MatchPhase
{
    Lobby,
    Countdown,
    Round,
    RoundEnd,
    Finished,
    Armory
};
enum class AbortReason
{
    None,
    PeerLeft,
    PeerTimeout
};

enum class WeaponChoice
{
    Pending,
    Spinning,
    Ready,
    Revealing
};

enum class UpgradeKind
{
    Continue,
    Weapon,
    Ability,
    ActiveQ,
    ActiveE
};

enum class MatchFormat : unsigned
{
    Duel,
    FreeForAll,
    Teams
};
enum class SlotKind : unsigned
{
    Empty,
    Human,
    Bot
};

struct MatchRules
{
    unsigned wins = 15;
    GameMode mode = GameMode::Duel;
    unsigned stages = 14, controlGoal = 180;

    MatchFormat format = MatchFormat::Duel;
    unsigned participants = 2;
    bool friendlyFire = false;

    bool valid() const
    {
        return unsigned(format) < 3 &&
               ((format == MatchFormat::Duel && participants == 2) ||
                (format == MatchFormat::FreeForAll && participants >= 3 && participants <= 4) ||
                (format == MatchFormat::Teams && participants == 4)) &&
               unsigned(mode) < 4 && (wins == 5 || wins == 10 || wins == 15 || wins == 20) &&
               (stages == 7 || stages == 14 || stages == 21) &&
               (controlGoal == 120 || controlGoal == 180 || controlGoal == 240);
    }
};

struct RespawnState
{
    unsigned life = 0;
    float remaining = 0, decisionTime = 0;
    bool choosing = false;
};

struct ModeState
{
    ChaosKind chaos = ChaosKind::None, nextChaos = ChaosKind::None;
    double elapsed = 0, zoneRemaining = 30;
    unsigned zone = 0;
    std::array<double, maxPlayers> control{};
    std::array<RespawnState, maxPlayers> respawn{};
};

struct WorldSnapshot
{
    std::uint64_t tick = 0;
    std::uint32_t round = 0;
    MatchPhase phase = MatchPhase::Lobby;
    float remaining = 0;
    std::array<unsigned, maxPlayers> score{};
    std::array<bool, maxPlayers> connected{}, ready{};
    PlayerId winner = 0;
    AbortReason aborted = AbortReason::None;
    std::vector<Player> players;
    std::vector<Bullet> bullets;
    std::array<std::uint64_t, maxPlayers> inputAck{}, actionAck{};
    std::array<std::string, maxPlayers> names{"Игрок 1", "Игрок 2", "Игрок 3", "Игрок 4"};
    MapId map = MapId::Duel;
    bool voting = false;
    std::array<int, maxPlayers> votes{{-1, -1, -1, -1}};
    std::uint64_t selectionRevision = 0, stateSequence = 0;
    std::vector<FirePatch> fires;
    std::vector<Corpse> corpses;
    std::vector<PhysicsPiece> pieces;
    std::array<WeaponChoice, maxPlayers> weaponChoice{};
    std::array<WeaponKind, maxPlayers> loadout{}, rolled{};
    std::array<float, maxPlayers> spinRemaining{};
    std::array<Abilities, maxPlayers> abilities{};
    std::array<UpgradeKind, maxPlayers> upgrade{};
    std::array<AbilityKind, maxPlayers> rolledAbility{{AbilityKind::None, AbilityKind::None}};
    std::array<SlotKind, maxPlayers> slots{};
    std::array<TeamId, maxPlayers> teams{{1, 2, 1, 2}};
    std::array<unsigned, maxPlayers> botDifficulty{{1, 1, 1, 1}};
    std::array<unsigned, 2> teamScore{};
    TeamId winnerTeam = 0;
    PlayerId host = 1;
    float decisionRemaining = 0;
    MatchRules rules;
    ModeState mode;
    std::vector<WorldEntity> entities;
    TerrainState terrain;
    std::array<ActiveLoadout, maxPlayers> activeLoadout{};
    std::array<ActiveKind, maxPlayers> rolledActive{};
};

// Чистые правила дуэли. Сеть назначает слоты; только сервер меняет Match.
class Match
{
public:
    static constexpr float aftermathSeconds = 4, spinSeconds = 7.4f, wheelSeconds = 2.4f;
    static constexpr float abilityReviewSeconds = 5;

    explicit Match(bool voting = false, std::uint32_t seed = 0)
        : voting_(voting), random_(seed ? seed : std::random_device{}())
    {
        visualSeed_ = seed ? (seed ^ 0xa31b29f7u) : std::random_device{}();
        world_.setVisualSeed(visualSeed_);
    }

    bool configureSlot(PlayerId actor, PlayerId slot, SlotKind kind, unsigned difficulty, TeamId team,
                       std::uint64_t revision);
    bool selectTeam(PlayerId actor, TeamId team, std::uint64_t revision);

    PlayerId host() const
    {
        return host_;
    }

    void transferHost(PlayerId id)
    {
        host_ = id;
    }

    void replaceWithBot(PlayerId id);
    bool selectRules(PlayerId id, MatchRules rules, std::uint64_t revision);
    bool selectMap(PlayerId id, MapId map, std::uint64_t revision);
    bool changeMap(PlayerId id, std::uint64_t revision);
    std::vector<GameEvent> takeEvents();
    void connect(PlayerId id);
    void disconnect(PlayerId id, AbortReason reason = AbortReason::PeerLeft);
    void setReady(PlayerId id, bool ready);
    void setInput(PlayerId id, const PlayerInput& input);
    void clearInput(PlayerId id);
    bool action(PlayerId id, std::uint32_t round, std::uint64_t sequence, bool jump, bool shoot, Vec2 aim,
                bool reload = false, unsigned activeMask = 0, unsigned life = ~0u);
    void update(float dt);
    bool chooseWeapon(PlayerId id, bool reroll, std::uint32_t round, std::uint64_t revision);
    bool chooseUpgrade(PlayerId id, UpgradeKind kind, std::uint32_t round, std::uint64_t revision,
                       unsigned life = ~0u);
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
    void rebuildWorld();

    unsigned side(unsigned slot) const
    {
        return rules_.format == MatchFormat::Teams ? teams_[slot] - 1 : slot;
    }

    std::array<SlotKind, maxPlayers> slots_{};
    std::array<TeamId, maxPlayers> teams_{{1, 2, 1, 2}};
    std::array<unsigned, maxPlayers> botDifficulty_{{1, 1, 1, 1}};
    std::array<unsigned, 2> teamScore_{};
    TeamId winnerTeam_ = 0;
    PlayerId host_ = 1;
    float decisionRemaining_ = 0;
    void updateChoices(float dt);
    void updateControl(float dt);
    void prepareChaos();
    ModeState mode_;
    void collectEvents();
    std::array<WeaponKind, maxPlayers> loadout_{}, rolled_{};
    std::array<WeaponChoice, maxPlayers> weaponChoice_{};
    std::array<float, maxPlayers> spinRemaining_{};
    std::array<Abilities, maxPlayers> abilities_{};
    std::array<UpgradeKind, maxPlayers> upgrade_{};
    std::array<AbilityKind, maxPlayers> rolledAbility_{{AbilityKind::None, AbilityKind::None}};
    std::array<ActiveLoadout, maxPlayers> activeLoadout_{};
    std::array<ActiveKind, maxPlayers> rolledActive_{};
    MatchRules rules_;
    World world_{Arena::create(MapId::Duel), 2};
    bool voting_ = false, rematch_ = false;
    std::array<int, maxPlayers> votes_{{-1, -1, -1, -1}};
    std::uint64_t selectionRevision_ = 0;
    std::vector<GameEvent> events_;
    MatchPhase phase_ = MatchPhase::Lobby;
    AbortReason aborted_ = AbortReason::None;
    std::array<bool, maxPlayers> connected_{}, ready_{};
    std::array<unsigned, maxPlayers> score_{};
    std::array<std::uint64_t, maxPlayers> lastAction_{};
    std::uint64_t tick_ = 0;
    std::uint32_t round_ = 0;
    unsigned roundsPlayed_ = 0;
    float remaining_ = 0;
    PlayerId winner_ = 0;
    std::mt19937 random_;
    unsigned visualSeed_ = 0;
};
} // namespace cubeshot
