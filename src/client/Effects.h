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
struct Wound
{
    Vec2 local;
    float radius = 4;
};

struct Pose
{
    Vec2 previous{}, corpse{}, corpseVelocity{};
    float cycle = 0, land = 0, shot = 0, hit = 0, death = 0;
    bool grounded = false, alive = true, initialized = false;
    std::vector<Wound> wounds;
};

struct Particle
{
    Vec2 position, velocity;
    float life = 0;
    bool blood = false;
    bool chip = false;
    PlayerId owner = 0;
    float size = 3, angle = 0, spin = 0;
};

struct Stain
{
    Vec2 position;
    float width = 0, height = 0;
    float angle = 0;
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

    Vec2 shakeOffset() const;

    bool bloodVisible() const
    {
        return bloodVisible_;
    }

private:
    float random(float low, float high);
    void burst(Vec2 at, Vec2 direction, int count, bool blood, PlayerId owner = 0, bool chip = false);
    std::uint32_t random_ = 719;
    float shake_ = 0, time_ = 0;
    bool bloodVisible_ = true;
    std::map<PlayerId, Pose> poses_;
    std::vector<Particle> particles_;
    std::deque<Stain> stains_;
    std::set<std::pair<std::uint32_t, std::uint64_t>> seen_;
    std::set<std::tuple<std::uint32_t, PlayerId, std::uint64_t>> predicted_;
};
} // namespace cubeshot::client
