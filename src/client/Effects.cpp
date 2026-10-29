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
    auto it = poses_.find(id);
    return it == poses_.end() ? empty : it->second;
}

Vec2 Effects::shakeOffset() const
{
    return {std::sin(time_ * 131) * shake_, std::cos(time_ * 173) * shake_ * .65f};
}

void Effects::burst(Vec2 at, Vec2 direction, int count, bool blood, PlayerId owner, bool chip)
{
    count = std::min(count, static_cast<int>(burstBudget_));
    burstBudget_ -= count;
    if (blood && splashes_.size() < 32)
        splashes_.push_back({at, std::atan2(direction.y, direction.x), random(42, 76), .18f,
                             static_cast<unsigned>(random(0, 5.99f))});
    for (int i = 0; i < count && particles_.size() < maxParticles; ++i)
    {
        Particle p;
        p.position = at;
        p.velocity = {direction.x * random(70, 250) + random(-90, 90),
                      direction.y * random(40, 170) - random(40, 210)};
        p.life = random(.35f, 1.2f);
        p.blood = blood;
        p.chip = chip;
        p.owner = owner;
        p.size = chip ? random(4, 9) : blood ? random(1.7f, 4.5f) : random(1, 2.5f);
        p.angle = random(-3.14f, 3.14f);
        p.spin = random(-7, 7);
        p.sprite = blood  ? (i % 11 == 0 ? 21u : 6u + static_cast<unsigned>(random(0, 5.99f)))
                   : chip ? 21
                          : 20;
        if (blood && i % 11 == 0)
        {
            p.chip = true;
            p.size = random(6, 12);
        }
        particles_.push_back(p);
    }
}

void Effects::update(float dt, const std::vector<Player>& players, const Arena& arena,
                     const std::vector<GameEvent>& events, PlayerId own, bool blood, bool shake)
{
    bloodVisible_ = blood;
    dt = std::clamp(dt, 0.f, .1f);
    time_ += dt;
    burstBudget_ = 180;
    shake_ = shake ? std::max(0.f, shake_ - dt * 35) : 0;
    if (!blood)
    {
        stains_.clear();
        drips_.clear();
        splashes_.clear();
        particles_.erase(
            std::remove_if(particles_.begin(), particles_.end(), [](const auto& p) { return p.blood; }),
            particles_.end());
    }
    for (auto& b : blasts_)
        b.life -= dt;
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(), [](const auto& b) { return b.life <= 0; }),
                  blasts_.end());
    for (auto& s : splashes_)
        s.life -= dt;
    splashes_.erase(
        std::remove_if(splashes_.begin(), splashes_.end(), [](const auto& s) { return s.life <= 0; }),
        splashes_.end());
    for (auto& s : stains_)
        s.age += dt;
    for (auto& d : drips_)
    {
        d.age += dt;
        d.length = std::min(d.maximum, d.length + dt * 10);
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
            pose.cycle += std::abs(p.position.x - pose.previous.x) / 22;
        if (p.grounded && !pose.grounded && p.alive())
            pose.land = .16f;
        pose.land = std::max(0.f, pose.land - dt);
        pose.shot = std::max(0.f, pose.shot - dt);
        pose.hit = std::max(0.f, pose.hit - dt);
        if (!p.alive() && pose.alive)
            pose.death = .001f;
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
                shake_ = std::max(shake_, meleeWeapon(e.weapon) ? 1.5f : 2.5f);
        }
        else if (e.kind == EventKind::Hit)
        {
            poses_[e.target].hit = .12f;
            Vec2 at = e.position;
            auto found =
                std::find_if(players.begin(), players.end(), [&](const auto& p) { return p.id == e.target; });
            if (found != players.end())
            {
                const auto point = found->bodyPoint(e.local);
                at = {found->position.x + point.x, found->position.y + point.y};
            }
            if (blood && !incendiaryWeapon(e.weapon))
                burst(at, e.direction, std::clamp(12 + e.damage * 2, 16, 80), true, e.target);
            if (!incendiaryWeapon(e.weapon))
                burst(at, e.direction, 2, false, e.target, true);
            if (shake && e.target == own)
                shake_ = std::max(shake_, 5.f);
        }
        else if (e.kind == EventKind::SurfaceImpact)
        {
            if (e.weapon != WeaponKind::Flamethrower)
                burst(e.position, {-e.direction.x, -e.direction.y}, 4, false);
        }
        else if (e.kind == EventKind::Explosion)
        {
            if (blasts_.size() < 16)
                blasts_.push_back({e.position, .45f});
            if (shake)
                shake_ = std::max(shake_, 8.f);
        }
        else if (e.kind == EventKind::Death)
        {
            if (blood && !incendiaryWeapon(e.weapon))
                burst({e.position.x + 24, e.position.y + 24}, e.direction, 120, true, e.target);
            else if (incendiaryWeapon(e.weapon))
                burst({e.position.x + 24, e.position.y + 24}, {0, -1}, 14, false);
        }
        else if ((e.kind == EventKind::CorpseHit || e.kind == EventKind::CorpseBreak) && blood &&
                 !incendiaryWeapon(e.weapon))
            burst(e.position, e.direction, e.kind == EventKind::CorpseBreak ? 90 : 30, true, e.target);
    }
    if (dt <= 0)
        return;
    smokeAccumulator_ += dt;
    if (smokeAccumulator_ >= .1f)
    {
        smokeAccumulator_ = 0;
        for (const auto& player : players)
            if (player.burning > 0 && player.alive() && particles_.size() < maxParticles)
            {
                Particle p;
                p.position = {player.position.x + 24, player.position.y + 7};
                p.velocity = {random(-8, 8), random(-38, -20)};
                p.life = 1.2f;
                p.size = 20;
                p.smoke = true;
                particles_.push_back(p);
            }
    }
    for (auto& p : particles_)
    {
        p.life -= dt;
        p.angle += p.spin * dt;
        if (p.smoke)
        {
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;
            p.size += dt * 13;
            continue;
        }
        p.velocity.y += 850 * dt;
        const Vec2 to{p.position.x + p.velocity.x * dt, p.position.y + p.velocity.y * dt};
        float nearest = 2;
        const Rect* surface = nullptr;
        for (const auto& box : arena.platforms)
            if (std::max(p.position.x, to.x) + 1 >= box.position.x &&
                std::min(p.position.x, to.x) - 1 <= box.position.x + box.size.x &&
                std::max(p.position.y, to.y) + 1 >= box.position.y &&
                std::min(p.position.y, to.y) - 1 <= box.position.y + box.size.y)
                if (auto hit = collision::segmentHit(p.position, to, box, 1); hit && *hit < nearest)
                {
                    nearest = *hit;
                    surface = &box;
                }
        if (nearest <= 1)
        {
            if (p.blood)
            {
                Vec2 at{p.position.x + (to.x - p.position.x) * nearest,
                        p.position.y + (to.y - p.position.y) * nearest};
                if (stains_.size() >= maxStains)
                    stains_.pop_front();
                const bool top = surface && std::abs(at.y - surface->position.y) < 3;
                stains_.push_back({at, random(12, 32), random(6, 13), top ? random(-.2f, .2f) : 1.5708f,
                                   static_cast<unsigned>(random(0, 5.99f)), 0});
                if (top && surface->position.y > 20 && surface->size.y < 100 &&
                    (at.x - surface->position.x < 12 || surface->position.x + surface->size.x - at.x < 12))
                {
                    if (drips_.size() >= 64)
                        drips_.pop_front();
                    const float edge = at.x - surface->position.x < 12
                                           ? surface->position.x + 1
                                           : surface->position.x + surface->size.x - 1;
                    drips_.push_back({{edge, surface->position.y + 2}, 0, random(15, 32), 0});
                }
            }
            p.life = 0;
        }
        p.position = to;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [&](const auto& p)
                                    {
                                        return p.life <= 0 || p.position.x < arena.bounds.position.x ||
                                               p.position.x > arena.bounds.position.x + arena.bounds.size.x ||
                                               p.position.y > arena.bounds.position.y + arena.bounds.size.y;
                                    }),
                     particles_.end());
}
} // namespace cubeshot::client
