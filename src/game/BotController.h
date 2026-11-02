#pragma once
#include "game/BotNavigation.h"
#include "game/World.h"
#include <deque>
#include <optional>
#include <random>
#include <unordered_set>

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
    float reaction, aimErrorDegrees, shotInterval, dodgeHorizon;
};

BotTuning botTuning(BotDifficulty difficulty);

struct BotTarget
{
    Vec2 position, velocity, size;
    bool grounded;
    WeaponKind weapon = WeaponKind::Pistol;
    Vec2 aim{1, 0};
    int health = 100;
    bool charging = false, overheated = false;
    std::uint32_t shots = 0;
};

struct BotObservation
{
    Player self;
    std::optional<BotTarget> target;
    std::vector<Bullet> bullets;
    std::vector<FirePatch> fires;
};

BotObservation observeBot(const World& world, PlayerId self, PlayerId opponent);

struct BotWeaponProfile
{
    float minimum, maximum;
};

BotWeaponProfile botWeaponProfile(WeaponKind weapon);

struct BotAim
{
    Vec2 direction;
    float flightTime;
};

std::optional<BotAim> botAim(const Player& self, const BotTarget& target, const Arena& arena,
                             float errorRadians = 0);

// Observes the whole arena, like the human. Never receives future human input.
class BotController
{
public:
    BotController(Arena arena, BotDifficulty difficulty, std::uint32_t seed);
    void reset();
    PlayerInput update(const BotObservation& observation, float dt);

private:
    Vec2 chooseGoal(const Player& self, const std::optional<BotTarget>& target);
    PlayerInput navigate(const Player& self, Vec2 goal, float dt);
    void avoidBullets(const BotObservation& observation, PlayerInput& input);
    void cancelRoute(bool failed);
    float hazardCost(Vec2 position) const;
    bool hard_ = false, enemyCharging_ = false;
    WeaponKind lastWeapon_ = WeaponKind::Pistol, enemyWeapon_ = WeaponKind::Pistol;
    std::uint32_t enemyShots_ = 0;
    float enemyShotAt_ = -100, coverSince_ = -1, attackUntil_ = 0;
    std::vector<float> routeHazards_;
    Arena arena_;
    BotTuning tuning_;
    std::mt19937 random_;
    std::shared_ptr<const BotNavigation> navigation_;

    struct Perception
    {
        float time;
        std::optional<BotTarget> target;
        std::vector<Bullet> bullets;
        std::vector<FirePatch> fires;
    };

    std::deque<Perception> history_;
    Perception perceived_{};
    float time_ = 0, nextShot_ = 0, nextAim_ = 0, error_ = 0;
    float replan_ = 0, stuck_ = 0, avoidedUntil_ = 0, nextGoal_ = 0;
    Vec2 progress_{}, goal_{}, aim_{-1, 0};
    std::vector<BotTraversal> path_;
    std::size_t pathIndex_ = 0;
    int traversalTick_ = -1, avoidFrom_ = -1, avoidTo_ = -1;
    bool leftGround_ = false;
    float dodgeUntil_ = 0, dodgeMove_ = 0, nextDodgeJump_ = 0;
    std::unordered_set<BulletId> jumpedBullets_;
};
} // namespace cubeshot
