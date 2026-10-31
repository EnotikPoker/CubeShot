#pragma once
#include "game/Arena.h"
#include "game/Events.h"
#include "game/Player.h"
#include <deque>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace cubeshot::client
{
struct Pose
{
    Vec2 previous{};
    float cycle = 0, land = 0, shot = 0, hit = 0, death = 0;
    bool grounded = false, alive = true, initialized = false;
};

struct Particle
{
    Vec2 position, velocity;
    float life = 0;
    bool blood = false, chip = false;
    PlayerId owner = 0;
    float size = 3, angle = 0, spin = 0;
    unsigned sprite = 0;
    bool smoke = false;
};

struct Blast
{
    Vec2 position;
    float life = .45f;
};

struct Stain
{
    Vec2 position;
    float width = 0, height = 0, angle = 0;
    unsigned variant = 0;
    float age = 0;
};

struct Splash
{
    Vec2 position;
    float angle = 0, size = 40, life = .18f;
    unsigned variant = 0;
};

struct Drip
{
    Vec2 position;
    float length = 0, maximum = 24, age = 0;
};

class Effects
{
public:
    static constexpr std::size_t maxParticles = 2048, maxStains = 512;
    void clear();
    void update(float dt, const std::vector<Player>& players, const Arena& arena,
                const std::vector<GameEvent>& events, PlayerId own, bool blood = true, bool shake = true);
    const Pose& pose(PlayerId id) const;

    const std::vector<Particle>& particles() const
    {
        return particles_;
    }

    const std::deque<Stain>& stains() const
    {
        return stains_;
    }

    const std::vector<Blast>& blasts() const
    {
        return blasts_;
    }

    const std::vector<Splash>& splashes() const
    {
        return splashes_;
    }

    const std::deque<Drip>& drips() const
    {
        return drips_;
    }

    Vec2 shakeOffset() const;

    float time() const
    {
        return time_;
    }

    bool bloodVisible() const
    {
        return bloodVisible_;
    }

private:
    float random(float low, float high);
    void burst(Vec2 at, Vec2 direction, int count, bool blood, PlayerId owner = 0, bool chip = false);
    std::uint32_t random_ = 719;
    float shake_ = 0, time_ = 0, smokeAccumulator_ = 0;
    bool bloodVisible_ = true;
    std::map<PlayerId, Pose> poses_;
    std::vector<Particle> particles_;
    std::vector<Blast> blasts_;
    std::vector<Splash> splashes_;
    std::deque<Stain> stains_;
    std::deque<Drip> drips_;
    unsigned burstBudget_ = 180;
    std::set<std::pair<std::uint32_t, std::uint64_t>> seen_;
    std::set<std::tuple<std::uint32_t, PlayerId, std::uint64_t>> predicted_;
};
} // namespace cubeshot::client
