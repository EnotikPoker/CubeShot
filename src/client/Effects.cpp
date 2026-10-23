#include "client/Effects.h"
#include "game/Collision.h"
#include <algorithm>
#include <cmath>

namespace cubeshot::client
{
void Effects::clear()
{
    *this = Effects{};
}

float Effects::random(float low, float high)
{
    random_ = random_ * 1664525u + 1013904223u;
    return low + (high - low) * static_cast<float>(random_ >> 8) / 16777216.f;
}

const Pose& Effects::pose(PlayerId id) const
{
    static const Pose empty;
    const auto found = poses_.find(id);
    return found == poses_.end() ? empty : found->second;
}

void Effects::burst(Vec2 at, Vec2 direction, int count, bool blood)
{
    for (int i = 0; i < count && particles_.size() < 512; ++i)
        particles_.push_back({at,
                              {direction.x * random(60, 180) + random(-100, 100),
                               direction.y * random(40, 120) - random(60, 200)},
                              random(.35f, 1.f),
                              blood});
}

Vec2 Effects::shakeOffset() const
{
    return {std::sin(time_ * 131) * shake_, std::cos(time_ * 173) * shake_ * .65f};
}

void Effects::update(float dt, const std::vector<Player>& players, const Arena& arena,
                     const std::vector<GameEvent>& events, PlayerId own, bool blood, bool shake)
{
    dt = std::clamp(dt, 0.f, .1f);
    time_ += dt;
    shake_ = shake ? std::max(0.f, shake_ - dt * 35) : 0;
    if (!blood)
    {
        stains_.clear();
        particles_.erase(
            std::remove_if(particles_.begin(), particles_.end(), [](const auto& p) { return p.blood; }),
            particles_.end());
    }
    for (const auto& p : players)
    {
        auto& pose = poses_[p.id];
        if (!pose.initialized)
        {
            pose.previous = p.position;
            pose.grounded = p.grounded;
            pose.initialized = true;
        }
        if (p.grounded)
            pose.cycle += std::abs(p.position.x - pose.previous.x) / 22.f;
        if (p.grounded && !pose.grounded && p.alive())
            pose.land = .16f;
        pose.land = std::max(0.f, pose.land - dt);
        pose.shot = std::max(0.f, pose.shot - dt);
        pose.hit = std::max(0.f, pose.hit - dt);
        if (!p.alive() && pose.alive)
        {
            pose.corpse = p.position;
            pose.corpseVelocity = {p.velocity.x * .2f, -75};
            pose.death = .001f;
        }
        if (!p.alive())
        {
            pose.death += dt;
            pose.corpseVelocity.y += Player::gravity * dt;
            Rect body{{pose.corpse.x, pose.corpse.y + 30}, {48, 18}};
            const auto move =
                collision::moveVertically(body, pose.corpseVelocity.y * dt, arena.bounds, arena.platforms);
            pose.corpse.y = move.y - 30;
            if (move.blocked)
                pose.corpseVelocity.y = 0;
        }
        pose.alive = p.alive();
        pose.previous = p.position;
        pose.grounded = p.grounded;
    }
    for (const auto& e : events)
    {
        if (e.id && !seen_.insert({e.round, e.id}).second)
            continue;
        if (seen_.size() > 512)
            seen_.erase(seen_.begin());
        if (e.kind == EventKind::Shot)
        {
            const auto key = std::make_pair(e.round, e.shotSequence);
            if (e.actor == own && e.shotSequence)
            {
                if (e.id && predicted_.erase(key))
                    continue;
                if (!e.id)
                {
                    if (!predicted_.insert(key).second)
                        continue;
                    if (predicted_.size() > 128)
                        predicted_.erase(predicted_.begin());
                }
            }
            poses_[e.actor].shot = .1f;
            if (shake && e.actor == own)
                shake_ = std::max(shake_, 2.5f);
        }
        else if (e.kind == EventKind::Hit)
        {
            poses_[e.target].hit = .14f;
            if (blood)
                burst(e.position, e.direction, 18, true);
            if (shake && e.target == own)
                shake_ = std::max(shake_, 6.f);
        }
        else if (e.kind == EventKind::SurfaceImpact)
            burst(e.position, {-e.direction.x, -e.direction.y}, 5, false);
        else if (e.kind == EventKind::Death && blood)
            burst({e.position.x + 24, e.position.y + 24}, e.direction, 26, true);
    }
    for (auto& p : particles_)
    {
        p.life -= dt;
        p.velocity.y += 650 * dt;
        const Vec2 to{p.position.x + p.velocity.x * dt, p.position.y + p.velocity.y * dt};
        float nearest = 2;
        for (const auto& platform : arena.platforms)
            if (auto hit = collision::segmentHit(p.position, to, platform, 1))
                nearest = std::min(nearest, *hit);
        if (nearest <= 1)
        {
            if (p.blood)
            {
                if (stains_.size() >= 256)
                    stains_.pop_front();
                stains_.push_back({{p.position.x + (to.x - p.position.x) * nearest,
                                    p.position.y + (to.y - p.position.y) * nearest},
                                   std::abs(p.velocity.x) > std::abs(p.velocity.y) ? 2.f : random(4, 10),
                                   std::abs(p.velocity.x) > std::abs(p.velocity.y) ? random(4, 10) : 2.f});
            }
            p.life = 0;
        }
        p.position = to;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [&](const auto& p)
                                    {
                                        return p.life <= 0 || p.position.x < 0 ||
                                               p.position.y > arena.bounds.size.y ||
                                               p.position.x > arena.bounds.size.x;
                                    }),
                     particles_.end());
}
} // namespace cubeshot::client
