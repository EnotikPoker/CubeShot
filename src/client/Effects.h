#pragma once
#include "game/Arena.h"
#include "game/Events.h"
#include "game/Player.h"
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace cubeshot::client
{
struct Pose
{
    Vec2 previous{}, corpse{}, corpseVelocity{};
    float cycle = 0, land = 0, shot = 0, hit = 0, death = 0;
    bool grounded = false, alive = true, initialized = false;
};

struct Particle
{
    Vec2 position, velocity;
    float life = 0;
    bool blood = false;
};

struct Stain
{
    Vec2 position;
    float width = 0, height = 0;
};

class Effects
{
public:
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

private:
    float random(float low, float high);
    void burst(Vec2 at, Vec2 direction, int count, bool blood);
    std::uint32_t random_ = 719;
    float shake_ = 0, time_ = 0;
    std::map<PlayerId, Pose> poses_;
    std::vector<Particle> particles_;
    std::deque<Stain> stains_;
    std::set<std::pair<std::uint32_t, std::uint64_t>> seen_, predicted_;
};
} // namespace cubeshot::client
