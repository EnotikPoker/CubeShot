#include "game/AttackQueries.h"
#include "game/AttackStats.h"
#include "game/BodyPhysics.h"
#include "game/BoomerangMotion.h"
#include "game/BotController.h"
#include "game/BotQueries.h"
#include "game/Collision.h"
#include "game/ProjectileMotion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cubeshot
{
using namespace botdetail;

namespace botdetail
{
bool outside(Vec2 p, const Arena& arena)
{
    const auto& b = arena.bounds;
    return p.x < b.position.x + Bullet::radius || p.y < b.position.y + Bullet::radius ||
           p.x > b.position.x + b.size.x - Bullet::radius || p.y > b.position.y + b.size.y - Bullet::radius;
}

float terrainHit(Vec2 from, Vec2 to, const Arena& arena, float radius)
{
    float nearest = 2;
    for (const auto& r : arena.platforms)
        if (const auto hit = collision::segmentHit(from, to, r, radius))
            nearest = std::min(nearest, *hit);
    return nearest;
}

bool advanceKnownBullet(Bullet& bullet, float age, const Arena& arena, Vec2 owner)
{
    for (float elapsed = 0; elapsed < age; elapsed += step)
    {
        if (bullet.weapon == WeaponKind::FireBoomerang && !steerBoomerang(bullet, owner))
            return false;
        if (!advanceProjectileTerrain(bullet, arena, std::min(step, age - elapsed)))
            return false;
    }
    return bullet.lifetime > 0;
}

} // namespace botdetail

namespace
{
constexpr std::size_t dodgeSteps = 80;

struct FlightStep
{
    Vec2 from, to;
    float barrier;
    int damage;
    float radius;
};

struct Threat
{
    BulletId id;
    PlayerId shooter = 0;
    std::array<FlightStep, dodgeSteps> path;
    std::size_t count = 0;
    WeaponKind weapon = WeaponKind::Pistol;
    int damage = 20;
    float blastRadius = 240, power = 1;
    int blastDamage = 120;
};

struct BodyPath
{
    std::array<Vec2, dodgeSteps + 1> points;
    std::size_t count = 0;
    Vec2 size;
};

Threat predictThreat(Bullet bullet, float horizon, const Arena& arena, Vec2 owner)
{
    Threat out;
    out.id = bullet.id;
    out.shooter = bullet.shooterId;
    out.weapon = bullet.weapon;
    out.damage = bullet.damage;
    auto blast = explosionStats(bullet);
    out.blastRadius = blast.radius;
    out.blastDamage = blast.damage;
    out.power = bullet.power;
    for (float elapsed = 0; elapsed < horizon && out.count < dodgeSteps; elapsed += step)
    {
        if (bullet.weapon == WeaponKind::FireBoomerang && !steerBoomerang(bullet, owner))
            break;
        const auto from = bullet.position;
        const float dt = std::min(step, horizon - elapsed);
        if (bullet.lifetime <= dt)
        {
            if (explosiveWeapon(bullet.weapon))
                out.path[out.count++] = {from, from, 1, 0, bullet.collisionRadius};
            break;
        }
        const bool flying = advanceProjectileTerrain(bullet, arena, dt);
        out.path[out.count++] = {from, bullet.position, flying ? 2.f : 1.f,
                                 int(std::lround(bullet.impactDamage() * bullet.power)),
                                 bullet.collisionRadius};
        if (!flying)
            break;
    }
    return out;
}

BodyPath predictBody(Player self, const PlayerInput& input, float horizon, const Arena& arena)
{
    BodyPath out;
    out.points[0] = self.position;
    out.size = self.size;
    auto continuous = input;
    continuous.jump = continuous.shoot = continuous.reload = false;
    self.applyInput(continuous);
    self.requestAction(input.jump, false, input.aimDirection);
    const bool support = self.grounded;
    for (float elapsed = 0; elapsed < horizon && out.count < dodgeSteps; elapsed += step)
    {
        self.update(std::min(step, horizon - elapsed), arena, elapsed == 0 && support);
        out.points[++out.count] = self.position;
    }
    return out;
}

float expectedDamage(const Threat& threat, const BodyPath& body, const Arena& arena, const Player& self)
{
    float damage = 0;
    const auto count = std::min(threat.count, body.count);
    for (std::size_t n = 0; n < count; ++n)
    {
        const auto& flight = threat.path[n];
        const auto hit =
            collision::segmentHit({flight.from.x - body.points[n].x, flight.from.y - body.points[n].y},
                                  {flight.to.x - body.points[n + 1].x, flight.to.y - body.points[n + 1].y},
                                  {{0, 0}, body.size}, flight.radius);
        if (hit && *hit < flight.barrier)
            return self.invulnerability > 0 ? 0.f
                   : self.shields > 0 && threat.shooter != self.id
                       ? 2.f
                       : static_cast<float>(std::max(0, healthDamage(flight.damage, 1, self, threat.weapon) -
                                                            int(self.barrier))) +
                             (incendiaryWeapon(threat.weapon)
                                  ? healthDamage(12, 1, self, WeaponKind::Flamethrower, true)
                                  : 0.f);
        if (explosiveWeapon(threat.weapon) && flight.barrier <= 1)
        {
            const Vec2 blast{flight.from.x + (flight.to.x - flight.from.x) * flight.barrier,
                             flight.from.y + (flight.to.y - flight.from.y) * flight.barrier};
            const Vec2 nearest{std::clamp(blast.x, body.points[n].x, body.points[n].x + body.size.x),
                               std::clamp(blast.y, body.points[n].y, body.points[n].y + body.size.y)};
            bool shielded = false;
            for (const auto& wall : arena.platforms)
                if (auto t = collision::segmentHit(blast, nearest, wall, 0); t && *t < .98f)
                {
                    shielded = true;
                    break;
                }
            if (!shielded)
                damage =
                    std::max(damage, threat.blastDamage * threat.power *
                                         std::max(0.f, 1 - distance(blast, nearest) / threat.blastRadius));
        }
    }
    return damage <= 0 || self.invulnerability > 0 ? 0.f
           : self.shields > 0 && threat.shooter != self.id
               ? 2.f
               : static_cast<float>(std::max(
                     0, healthDamage(static_cast<int>(damage), 1, self, threat.weapon) - int(self.barrier)));
}

} // namespace

void BotController::avoidBullets(const BotObservation& observation, PlayerInput& input)
{
    auto exists = [&](BulletId id)
    {
        return std::any_of(observation.bullets.begin(), observation.bullets.end(),
                           [&](const Bullet& b) { return b.id == id; });
    };
    for (auto it = jumpedBullets_.begin(); it != jumpedBullets_.end();)
        if (!exists(*it))
            it = jumpedBullets_.erase(it);
        else
            ++it;
    if (time_ < dodgeUntil_)
        return;
    std::vector<Threat> known;
    known.reserve(perceived_.bullets.size());
    for (auto bullet : perceived_.bullets)
        if (exists(bullet.id) &&
            advanceKnownBullet(bullet, std::max(0.f, time_ - perceived_.time), arena_,
                               perceived_.target ? center(*perceived_.target) : bullet.position))
            known.push_back(predictThreat(bullet, tuning_.dodgeHorizon, arena_,
                                          perceived_.target ? center(*perceived_.target) : bullet.position));
    auto risk = [&](const PlayerInput& candidate)
    {
        float hits = 0;
        const auto body = predictBody(observation.self, candidate, tuning_.dodgeHorizon, arena_);
        for (const auto& b : known)
            hits += expectedDamage(b, body, arena_, observation.self);
        for (std::size_t n = 0; n <= body.count; n += 8)
            hits += hazardCost(body.points[n]) * 6;
        return hits;
    };
    if (known.empty() && perceived_.fires.empty())
    {
        // Route selection already avoids distant hazards. An immediate escape
        // must still work without gunfire, and must preserve the route's direction.
        if (observation.self.grounded && !input.jump && time_ >= nextDodgeJump_ &&
            hazardCost(observation.self.position) > 0)
        {
            auto exposure = [&](const PlayerInput& candidate)
            {
                const auto body = predictBody(observation.self, candidate, tuning_.dodgeHorizon, arena_);
                float total = 0;
                for (std::size_t n = 0; n <= body.count; n += 8)
                    total += hazardCost(body.points[n]);
                return total;
            };
            auto escape = input;
            escape.jump = true;
            if (exposure(escape) < exposure(input))
            {
                cancelRoute(false);
                input = escape;
                nextDodgeJump_ = time_ + .9f;
            }
        }
        return;
    }
    const auto initialBody = predictBody(observation.self, input, tuning_.dodgeHorizon, arena_);
    float initialRisk = risk(input);
    bool freshThreat = false;
    for (const auto& b : known)
        if (expectedDamage(b, initialBody, arena_, observation.self) > 0)
        {
            freshThreat = freshThreat || !jumpedBullets_.count(b.id);
        }
    if (!initialRisk)
        return;
    const bool canJump = observation.self.grounded && time_ >= nextDodgeJump_ &&
                         (freshThreat || hazardCost(observation.self.position) > 0);
    PlayerInput best = input;
    auto bestRisk = initialRisk;
    float bestCost = std::numeric_limits<float>::max();
    for (bool jump : {false, true})
        for (float move : {input.moveDirection, 0.f, -1.f, 1.f})
        {
            if (jump && !canJump)
                continue;
            PlayerInput candidate{move, jump, false, input.aimDirection};
            const auto danger = risk(candidate);
            const float cost = std::abs(move - input.moveDirection) + (jump ? 2.f : 0.f);
            if (danger < initialRisk && (danger < bestRisk || (danger == bestRisk && cost < bestCost)))
            {
                best = candidate;
                bestRisk = danger;
                bestCost = cost;
            }
        }
    if (bestRisk >= initialRisk)
        return; // A pointless jump is not an evasive action.
    cancelRoute(false);
    input = best;
    dodgeMove_ = best.moveDirection;
    dodgeUntil_ = time_ + (best.jump ? .35f : .18f);
    if (best.jump)
    {
        nextDodgeJump_ = time_ + .9f;
        for (const auto& b : known)
            jumpedBullets_.insert(b.id);
    }
}

} // namespace cubeshot
