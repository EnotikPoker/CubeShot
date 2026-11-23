#include "game/AttackStats.h"
#include "game/BodyPhysics.h"
#include "game/BotController.h"
#include "game/BotQueries.h"
#include "game/ProjectileMotion.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
using namespace botdetail;

namespace
{
std::optional<BotAim> bombPlan(const Player& self, const BotTarget& target, const Arena& arena)
{
    std::optional<BotAim> best;
    float side = center(target).x >= self.weaponOrigin().x ? 1.f : -1.f;
    for (float degrees : {-75.f, -60.f, -45.f, -30.f, -15.f, 0.f, 15.f, 30.f})
    {
        float a = degrees * pi / 180;
        Vec2 dir{std::cos(a) * side, std::sin(a)};
        auto b = self.shotPrototype(dir);
        b.active = ActiveKind::Bomb;
        b.weapon = WeaponKind::GrenadeLauncher;
        b.damage = 60;
        b.velocity = {dir.x * 520, dir.y * 520};
        b.acceleration = 900;
        b.lifetime = 1.1f;
        b.collisionRadius = 5;
        b.ricochets = b.penetrations = 0;
        Player enemy = predictedTarget(target);
        enemy.id = self.id == 1 ? 2 : 1;
        auto own = self;
        own.clearInput();
        std::vector<Player> bodies{own, enemy};
        for (unsigned tick = 0; tick < 134; ++tick)
        {
            auto from = b.position;
            auto next = b;
            next.update(step);
            auto& victim = bodies[bodies[0].id == enemy.id ? 0 : 1];
            auto& owner = bodies[bodies[0].id == self.id ? 0 : 1];
            auto hit = collision::segmentHit(from, next.position, victim.bounds(), b.collisionRadius);
            bool direct = hit && *hit < terrainHit(from, next.position, arena, b.collisionRadius);
            if (direct)
                b.position = {from.x + (next.position.x - from.x) * *hit,
                              from.y + (next.position.y - from.y) * *hit};
            if (direct || next.lifetime <= 0)
            {
                auto blast = explosionStats(b);
                auto good = blastEffect(b.position, victim, arena, direct ? victim.id : 0, blast.radius,
                                        blast.damage);
                auto risk = blastEffect(b.position, owner, arena, 0, blast.radius, blast.damage);
                if (!risk.damage && good.damage >= 10 && (!best || good.damage > best->expectedDamage))
                    best = BotAim{dir, tick * step, float(good.damage)};
                break;
            }
            if (!advanceProjectileTerrain(b, arena, step))
                break;
            simulateBodies(bodies, arena, step);
        }
    }
    return best;
}
} // namespace

void BotController::useActives(const BotObservation& observation, PlayerInput& input)
{
    if (time_ < nextActive_ || !perceived_.target)
        return;
    nextActive_ = time_ + .1f;
    const auto& self = observation.self;
    const auto& target = *perceived_.target;
    auto enemy = predictedTarget(target);
    enemy.id = self.id == 1 ? 2 : 1;
    auto origin = self.weaponOrigin(), at = center(target);
    float gap = distance(origin, at);
    auto profile = abilityProfile(self.weapon.kind, self.abilities);
    bool melee = meleeWeapon(self.weapon.kind), close = melee || self.weapon.kind == WeaponKind::Shotgun ||
                                                        self.weapon.kind == WeaponKind::Flamethrower;
    bool danger = false;
    for (const auto& b : perceived_.bullets)
        if (distance(b.position, origin) < 240 &&
            (origin.x - b.position.x) * b.velocity.x + (origin.y - b.position.y) * b.velocity.y > 0)
            danger = true;
    bool mobility = false;
    for (unsigned slot = 0; slot < 2; ++slot)
    {
        auto ability = self.actives[slot];
        if (ability.cooldown > 0 || ability.kind == ActiveKind::None)
            continue;
        bool cast = false, aimOverride = false;
        Vec2 aim = input.aimDirection;
        switch (ability.kind)
        {
            case ActiveKind::Heal:
                cast = self.health <= self.healthLimit() - 25 ||
                       ((self.burning > 0 || self.bleeding > 0) && self.health < self.healthLimit() - 5);
                break;
            case ActiveKind::Barrier:
                cast = self.barrier <= 0 &&
                       (danger || target.charging || (meleeWeapon(target.weapon) && gap < 180) ||
                        (self.health < self.healthLimit() * .6f && gap < 700));
                break;
            case ActiveKind::Wave:
                cast =
                    gap < 150 &&
                    (!close || self.health < 40 || target.health <= self.weapon.damage + 15 ||
                     self.weapon.reloading) &&
                    blastEffect(origin, enemy, arena_, 0, 150 * self.abilities.blastRadius(), 15).damage > 0;
                break;
            case ActiveKind::Slam:
                cast =
                    (self.grounded ? gap < 110 : std::abs(at.x - origin.x) < 100 && at.y > origin.y + 20) &&
                    !self.slamming;
                break;
            case ActiveKind::Harpoon:
                if (close && gap > profile.maximum && gap < 600)
                {
                    auto future = predictBotTarget(target, perceived_.self, gap / 1100, arena_);
                    Vec2 d{future.weaponOrigin().x - origin.x, future.weaponOrigin().y - origin.y};
                    float n = std::max(.01f, std::hypot(d.x, d.y));
                    aim = {d.x / n, d.y / n};
                    cast = terrainHit(origin, future.weaponOrigin(), arena_, 3) > 1;
                    aimOverride = cast;
                }
                break;
            case ActiveKind::Bomb:
                if (gap > 190 && gap < 700)
                    if (auto plan = bombPlan(self, target, arena_))
                    {
                        aim = plan->direction;
                        cast = aimOverride = true;
                    }
                break;
            case ActiveKind::Blink:
            case ActiveKind::Dash:
            {
                if (mobility)
                    break;
                bool escape = (meleeWeapon(target.weapon) && !close && gap < 200) || danger ||
                              self.weapon.reloading || self.weapon.overheated;
                bool approach = (close && gap > profile.maximum + 70) || distance(self.position, goal_) > 200;
                if (!escape && !approach)
                    break;
                Vec2 d = escape ? Vec2{origin.x - at.x, origin.y - at.y}
                                : Vec2{goal_.x - self.position.x, goal_.y - self.position.y};
                if (std::hypot(d.x, d.y) < 40)
                    d = {at.x - origin.x, at.y - origin.y};
                float n = std::max(.01f, std::hypot(d.x, d.y));
                aim = {d.x / n, d.y / n};
                if (ability.kind == ActiveKind::Blink)
                {
                    if (auto to = blinkDestination(self, aim, arena_, {enemy}))
                    {
                        float old = escape ? gap : distance(self.position, goal_),
                              next = escape ? distance(*to, target.position) : distance(*to, goal_);
                        cast = escape ? next > old + 80 : next < old - 80;
                        if (cast && hazardCost(*to) > hazardCost(self.position) + .1f)
                            cast = false;
                    }
                }
                else
                {
                    auto probe = self;
                    probe.aimDirection = aim;
                    probe.activate(slot, arena_, {enemy});
                    std::vector<Player> bodies{probe, enemy};
                    for (int i = 0; i < 20; ++i)
                        simulateBodies(bodies, arena_, step);
                    for (const auto& p : bodies)
                        if (p.id == self.id)
                        {
                            float moved = distance(p.position, self.position);
                            cast = moved > 80 && hazardCost(p.position) <= hazardCost(self.position) + .1f;
                        }
                }
                if (cast)
                    mobility = aimOverride = true;
                break;
            }
            default:
                break;
        }
        if (cast)
        {
            if (slot)
                input.activeE = true;
            else
                input.activeQ = true;
            if (aimOverride)
            {
                input.aimDirection = aim;
                aim_ = aim;
                input.shoot = input.triggerHeld = false;
            }
        }
    }
}
} // namespace cubeshot
