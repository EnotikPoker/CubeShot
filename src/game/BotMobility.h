#pragma once
#include "game/BotNavigation.h"
#include "game/Combat.h"
#include "game/Player.h"
#include <memory>
#include <optional>
#include <vector>

namespace cubeshot
{
// Bounded, incremental search over actual weapon/physics states. No future enemy input.
class BotMobility
{
public:
    explicit BotMobility(Arena arena, bool hard);
    void reset();
    bool control(const Player& self, Vec2 goal, const std::vector<Bullet>& threats,
                 const std::vector<FirePatch>& fires, PlayerInput& input, float dt,
                 const Player* opponent = nullptr);

    unsigned lastSteps() const
    {
        return lastSteps_;
    }

    bool executing() const
    {
        return !playback_.empty();
    }

private:
    struct Program
    {
        float move, angle, delay, duration;
        bool baseline = false, jump = true;
    };

    struct Simulation
    {
        Player player;
        std::optional<Player> opponent;
        Program program;
        std::vector<Bullet> threats, rockets;
        std::vector<PlayerInput> inputs;
        std::vector<Vec2> positions;
        int tick = 0, align = 0, shots = 0, poweredTick = -1;
        bool jumped = false, cleared = false;
        bool airborne = false, blasted = false, landed = false;
        float burning = 0, burnClock = 0;
    };

    void begin(const Player& self, Vec2 goal, const std::vector<Bullet>& threats,
               const std::vector<FirePatch>& fires, const PlayerInput& baseline);
    bool advance(Simulation& simulation);
    float routeTime(Vec2 from, Vec2 to) const;
    Simulation simulateProgram(const Player& self, Program program) const;
    Arena arena_;
    bool hard_;
    std::shared_ptr<const BotNavigation> navigation_;
    std::optional<Simulation> current_;
    std::optional<Program> bestProgram_;
    bool validating_ = false;
    std::vector<Program> programs_;
    std::vector<FirePatch> fires_;
    std::vector<Bullet> threats_;
    Player root_;
    std::optional<Player> opponent_;
    Vec2 goal_{};
    std::vector<PlayerInput> best_, playback_;
    std::vector<Vec2> bestPositions_, expected_;
    std::size_t next_ = 0, frame_ = 0;
    float retry_ = 0, baseline_ = 0, bestCost_ = 1e9f;
    int bestHealth_ = 0, baselineHealth_ = 100;
    unsigned lastSteps_ = 0;
};
} // namespace cubeshot
