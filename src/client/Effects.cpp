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

void Effects::burst(Vec2 at, Vec2 direction, int count, bool blood, PlayerId owner, bool chip)
{
    for (int i = 0; i < count && particles_.size() < maxParticles; ++i)
        particles_.push_back({at,
                              {direction.x * random(60, 180) + random(-100, 100),
                               direction.y * random(40, 120) - random(60, 200)},
                              random(.35f, 1.f),
                              blood,
                              chip,
                              owner,
                              chip    ? random(4, 8)
                              : blood ? random(2, 5.5f)
                                      : 2.f,
                              random(-3.14f, 3.14f),
                              random(-8, 8)});
}

Vec2 Effects::shakeOffset() const
{
    return {std::sin(time_ * 131) * shake_, std::cos(time_ * 173) * shake_ * .65f};
}

void Effects::update(float dt, const std::vector<Player>& players, const Arena& arena,
                     const std::vector<GameEvent>& events, PlayerId own, bool blood, bool shake)
{
    bloodVisible_ = blood;
    dt = std::clamp(dt, 0.f, .1f);
    time_ += dt;
    shake_ = shake ? std::max(0.f, shake_ - dt * 35) : 0;
    if (!blood)
    {
        stains_.clear();
        for (auto& entry : poses_)
            entry.second.wounds.clear();
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
            pose.death += dt;
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
            const auto key = std::make_tuple(e.round, e.actor, e.volley ? e.volley : e.shotSequence);
            if (e.actor == own && (e.shotSequence || e.volley))
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
            {
                burst(e.position, e.direction, 35 + std::min(35, e.damage), true);
                const auto player = std::find_if(players.begin(), players.end(),
                                                 [&](const auto& p) { return p.id == e.target; });
                if (player != players.end())
                {
                    auto& wounds = poses_[e.target].wounds;
                    if (wounds.size() >= 12)
                        wounds.erase(wounds.begin());
                    wounds.push_back({{std::clamp(e.position.x - player->position.x, 4.f, 44.f),
                                       std::clamp(e.position.y - player->position.y, 4.f, 44.f)},
                                      random(4.f, 7.f)});
                }
            }
            burst(e.position, e.direction, 2, false, e.target, true);
            if (shake && e.target == own)
                shake_ = std::max(shake_, 6.f);
        }
        else if (e.kind == EventKind::SurfaceImpact)
            burst(e.position, {-e.direction.x, -e.direction.y}, 5, false);
        else if (e.kind == EventKind::Death && blood)
            burst({e.position.x + 24, e.position.y + 24}, e.direction, 100, true);
        else if ((e.kind == EventKind::CorpseHit || e.kind == EventKind::CorpseBreak) && blood)
            burst(e.position, e.direction, e.kind == EventKind::CorpseBreak ? 100 : 36, true);
    }
    for (auto& p : particles_)
    {
        p.life -= dt;
        p.velocity.y += 850 * dt;
        p.angle += p.spin * dt;
        const Vec2 to{p.position.x + p.velocity.x * dt, p.position.y + p.velocity.y * dt};
        float nearest = 2;
        for (const auto& platform : arena.platforms)
            if (auto hit = collision::segmentHit(p.position, to, platform, 1))
                nearest = std::min(nearest, *hit);
        if (nearest <= 1)
        {
            if (p.blood)
            {
                if (stains_.size() >= maxStains)
                    stains_.pop_front();
                stains_.push_back({{p.position.x + (to.x - p.position.x) * nearest,
                                    p.position.y + (to.y - p.position.y) * nearest},
                                   random(7, 18),
                                   random(4, 10),
                                   random(-.7f, .7f)});
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
