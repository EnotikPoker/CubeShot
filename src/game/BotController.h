#pragma once
#include "game/BotNavigation.h"
#include "game/World.h"
#include <deque>
#include <optional>
#include <random>

namespace cubeshot
{
enum class BotDifficulty
{
    Easy,
    Medium,
    Hard
};
std::u32string_view botDifficultyName(BotDifficulty difficulty);

struct BotTuning
{
    float reaction, aimErrorDegrees, shotInterval, memory, rangeMin, rangeMax, dodgeHorizon;
};

BotTuning botTuning(BotDifficulty difficulty);

struct BotTarget
{
    Vec2 position, velocity, size;
    bool grounded;
};

struct BotObservation
{
    Player self;
    std::optional<BotTarget> target;
    std::vector<Bullet> bullets;
};

BotObservation observeBot(const World& world, PlayerId self, PlayerId opponent);
bool botLineOfSight(Vec2 from, Vec2 to, const Arena& arena);

struct BotAim
{
    Vec2 direction;
    float flightTime;
};

std::optional<BotAim> botAim(const Player& self, const BotTarget& target, const Arena& arena,
                             float errorRadians = 0);

// Receives filtered observations only; never accesses World or human input.
class BotController
{
public:
    BotController(Arena arena, BotDifficulty difficulty, std::uint32_t seed);
    void reset();
    PlayerInput update(const BotObservation& observation, float dt);
    std::optional<Vec2> lastSeen() const;

private:
    Vec2 chooseGoal(const Player& self, const std::optional<BotTarget>& target);
    PlayerInput navigate(const Player& self, Vec2 goal, float dt);
    Arena arena_;
    BotTuning tuning_;
    std::mt19937 random_;
    std::shared_ptr<const BotNavigation> navigation_;

    struct Perception
    {
        float time;
        std::optional<BotTarget> target;
        std::vector<Bullet> bullets;
    };

    std::deque<Perception> history_;
    Perception perceived_{};
    std::optional<BotTarget> remembered_;
    float time_ = 0, seenAt_ = -100, nextShot_ = 0, nextAim_ = 0, error_ = 0;
    float replan_ = 0, stuck_ = 0, avoidedUntil_ = 0, nextPatrol_ = 0, nextGoal_ = 0;
    Vec2 progress_{}, patrol_{}, goal_{}, aim_{-1, 0};
    std::vector<BotTraversal> path_;
    std::size_t pathIndex_ = 0;
    int traversalTick_ = -1, avoidFrom_ = -1, avoidTo_ = -1;
};
} // namespace cubeshot
