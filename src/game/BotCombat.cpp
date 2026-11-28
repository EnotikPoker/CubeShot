#include "game/AttackQueries.h"
#include "game/AttackStats.h"
#include "game/BodyPhysics.h"
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
float shotSpeed(const Player& self)
{
    return self.weapon.bulletSpeed * self.modifiers.projectiles *
           ((ballisticWeapon(self.weapon.kind) || explosiveWeapon(self.weapon.kind))
                ? self.abilities.projectileSpeed()
                : 1.f);
}

// Solve actual drag and range falloff when aiming at an opening or target.
std::optional<BotAim> aimAtPoint(const Player& self, Vec2 point)
{
    const auto origin = self.weaponOrigin();
    Vec2 delta{point.x - origin.x, point.y - origin.y};
    float length = std::hypot(delta.x, delta.y);
    if (length < .01f)
        return {};
    Vec2 axis{delta.x / length, delta.y / length};
    float angle = std::atan2(delta.y, delta.x);
    for (int attempt = 0; attempt < 7; ++attempt)
    {
        Vec2 direction{std::cos(angle), std::sin(angle)};
        auto b = self.shotPrototype(direction);
        b.position = {origin.x + direction.x * self.weapon.barrel(),
                      origin.y + direction.y * self.weapon.barrel()};
        Vec2 previous = b.position;
        float before = (previous.x - origin.x) * axis.x + (previous.y - origin.y) * axis.y;
        bool reached = false;
        for (float time = 0; time < 2; time += step)
        {
            b.update(step);
            if (b.lifetime <= 0)
                break;
            float along = (b.position.x - origin.x) * axis.x + (b.position.y - origin.y) * axis.y;
            if (along >= length)
            {
                float part = std::clamp((length - before) / std::max(.0001f, along - before), 0.f, 1.f);
                Vec2 at{previous.x + (b.position.x - previous.x) * part,
                        previous.y + (b.position.y - previous.y) * part};
                float miss = axis.x * (at.y - point.y) - axis.y * (at.x - point.x);
                if (std::abs(miss) < .35f)
                    return BotAim{direction, time + step * part};
                angle -= std::atan2(miss, length);
                reached = true;
                break;
            }
            previous = b.position;
            before = along;
        }
        if (!reached)
            return {};
    }
    return {};
}

BotWeaponProfile abilityProfile(WeaponKind kind, const Abilities& abilities)
{
    auto profile = botWeaponProfile(kind);
    if (explosiveWeapon(kind))
    {
        profile.minimum *= abilities.blastRadius();
        profile.maximum *= abilities.projectileSpeed();
    }
    else
    {
        profile.minimum *= abilities.range();
        profile.maximum *= abilities.range();
    }
    return profile;
}

bool clearShot(const Player& self, Vec2 direction, float time, const Arena& arena)
{
    auto b = self.shotPrototype(direction);
    Vec2 origin = b.position;
    Vec2 muzzle{origin.x + direction.x * self.weapon.barrel(), origin.y + direction.y * self.weapon.barrel()};
    if (terrainHit(origin, muzzle, arena, b.collisionRadius) <= 1 && !b.penetrations && !b.ricochets)
        return false;
    b.position = muzzle;
    for (float t = 0; t < time; t += step)
        if (!advanceProjectileTerrain(b, arena, std::min(step, time - t)))
            return false;
    return true;
}

// Query the same contours, muzzle path and material trajectories as the actual shot.
float projectileHit(const Player& self, const Player& target, Vec2 direction, const Arena& arena,
                    float horizon)
{
    auto b = self.shotPrototype(direction);
    auto testSegment = [&](Vec2 from, Vec2 to)
    {
        auto hit = hitPolygon({from.x - target.position.x, from.y - target.position.y},
                              {to.x - target.position.x, to.y - target.position.y}, target.bodyContour(),
                              b.collisionRadius);
        return hit && *hit < terrainHit(from, to, arena, b.collisionRadius);
    };
    Vec2 muzzle{b.position.x + direction.x * self.weapon.barrel(),
                b.position.y + direction.y * self.weapon.barrel()};
    if (testSegment(b.position, muzzle))
        return static_cast<float>(healthDamage(b.impactDamage(), b.power, target, b.weapon));
    if (terrainHit(b.position, muzzle, arena, b.collisionRadius) <= 1)
        return 0;
    b.position = muzzle;
    for (float t = 0; t < horizon; t += step)
    {
        Vec2 from = b.position;
        auto preview = b;
        preview.update(step);
        if (preview.lifetime <= 0)
            break;
        if (testSegment(from, preview.position))
            return static_cast<float>(healthDamage(b.impactDamage(), b.power, target, b.weapon));
        bool active = b.weapon == WeaponKind::FireBoomerang
                          ? advanceKnownBullet(b, step, arena, self.weaponOrigin())
                          : advanceProjectileTerrain(b, arena, step);
        if (!active)
            break;
        // Penetration can move the projectile past a whole sheet in one step.
        if (distance(b.position, preview.position) > b.collisionRadius * 2 &&
            b.penetrations < preview.penetrations && testSegment(preview.position, b.position))
            return static_cast<float>(healthDamage(b.impactDamage(), b.power, target, b.weapon));
    }
    return 0;
}

std::optional<BotAim> rocketAim(const Player& self, const BotTarget& target, const Arena& arena, float error)
{
    const auto origin = self.weaponOrigin();
    const float radius = explosionRadius * self.abilities.blastRadius();
    if (distance(origin, center(target)) < radius + 30)
        return {};
    std::optional<BotAim> best;
    for (auto offset : {Vec2{0, -24}, Vec2{0, -3}, Vec2{-65, -3}, Vec2{65, -3}})
    {
        float flight =
            std::max(.01f, (distance(origin, center(target)) - self.weapon.barrel()) / shotSpeed(self));
        Vec2 delta{};
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            auto future = predictBotTarget(target, self, flight, arena);
            delta = {future.position.x + future.size.x / 2 + offset.x - origin.x,
                     future.position.y + future.size.y + offset.y - origin.y - 12.5f * flight * flight};
            flight = std::max(.01f, (std::hypot(delta.x, delta.y) - self.weapon.barrel()) / shotSpeed(self));
        }
        if (flight > 4)
            continue;
        const float angle = std::atan2(delta.y, delta.x);
        Vec2 direction{std::cos(angle), std::sin(angle)};
        auto rocket = self.shotPrototype(direction);
        rocket.position = {origin.x + direction.x * self.weapon.barrel(),
                           origin.y + direction.y * self.weapon.barrel()};
        if (terrainHit(origin, rocket.position, arena, rocket.collisionRadius) <= 1)
            continue;
        auto enemy = predictedTarget(target);
        enemy.id = target.id;
        auto own = self;
        own.clearInput();
        std::vector<Player> bodies{own, enemy};
        for (float elapsed = 0; elapsed < flight + .2f; elapsed += step)
        {
            const auto& enemyBody = bodies[bodies[0].id == enemy.id ? 0 : 1];
            const auto& ownBody = bodies[bodies[0].id == self.id ? 0 : 1];
            Vec2 from = rocket.position;
            rocket.update(step);
            if (rocket.lifetime <= 0)
                break;
            float hit = terrainHit(from, rocket.position, arena, rocket.collisionRadius);
            auto contact = hitPolygon(
                {from.x - enemyBody.position.x, from.y - enemyBody.position.y},
                {rocket.position.x - enemyBody.position.x, rocket.position.y - enemyBody.position.y},
                enemyBody.bodyContour(), rocket.collisionRadius);
            bool direct = contact && *contact < hit;
            if (direct)
                hit = *contact;
            if (hit <= 1)
            {
                Vec2 at{from.x + (rocket.position.x - from.x) * hit,
                        from.y + (rocket.position.y - from.y) * hit};
                auto victim =
                    blastEffect(at, enemyBody, arena, direct ? enemyBody.id : 0, radius, rocket.damage);
                auto risk = blastEffect(at, ownBody, arena, 0, radius, rocket.damage);
                float damage =
                    static_cast<float>(healthDamage(victim.damage, rocket.power, enemyBody, rocket.weapon));
                if (victim.damage > 0 && risk.damage == 0 && (!best || damage > best->expectedDamage))
                    best = BotAim{
                        {std::cos(angle + error), std::sin(angle + error)}, elapsed + hit * step, damage};
                break;
            }
            simulateBodies(bodies, arena, step);
        }
        if (best && best->expectedDamage >= 90)
            break;
    }
    return best;
}

} // namespace botdetail

namespace
{
std::optional<BotAim> grenadeAim(const Player& self, const BotTarget& target, const Arena& arena, float error)
{
    const float side = center(target).x >= self.weaponOrigin().x ? 1.f : -1.f;
    std::optional<BotAim> best;
    float bestDamage = 0;
    for (float degrees : {-70.f, -50.f, -30.f, -10.f, 10.f, 30.f})
    {
        float angle = degrees * pi / 180 + error;
        Vec2 direction{std::cos(angle) * side, std::sin(angle)};
        Bullet b = self.shotPrototype(direction);
        auto origin = b.position;
        b.position = {origin.x + direction.x * self.weapon.barrel(),
                      origin.y + direction.y * self.weapon.barrel()};
        if (terrainHit(origin, b.position, arena, b.collisionRadius) <= 1)
            continue;
        Player ownFuture = self;
        Player enemy = predictedTarget(target);
        enemy.id = target.id;
        for (int tick = 0; tick < 181; ++tick)
        {
            Vec2 from = b.position;
            b.update(step);
            Vec2 to = b.position;
            float nearest = 2;
            Rect wall{};
            bool direct = false;
            for (auto& r : arena.platforms)
                if (auto hit = collision::segmentHit(from, to, r, 3); hit && *hit < nearest)
                {
                    nearest = *hit;
                    wall = r;
                }
            if (auto hit = collision::segmentHit(from, to, enemy.bounds(), 3); hit && *hit < nearest)
            {
                nearest = *hit;
                direct = true;
            }
            if (nearest <= 1)
            {
                Vec2 at{from.x + (to.x - from.x) * nearest, from.y + (to.y - from.y) * nearest};
                if (direct)
                    b.position = at;
                else
                    bounceProjectile(b, at, wall, .55f);
            }
            if (direct || b.lifetime <= 0)
            {
                float radius = 180 * self.abilities.blastRadius();
                auto ownEffect = blastEffect(b.position, ownFuture, arena, 0, radius, 90);
                auto enemyEffect = blastEffect(b.position, enemy, arena, direct ? enemy.id : 0, radius, 90);
                if (ownEffect.damage == 0 && enemyEffect.damage > 0)
                {
                    float damage =
                        static_cast<float>(healthDamage(enemyEffect.damage, b.power, enemy, b.weapon));
                    if (damage > bestDamage + 1)
                    {
                        bestDamage = damage;
                        best = BotAim{direction, (tick + 1) * step, damage};
                    }
                }
                break;
            }
            std::vector<Player> bodies{ownFuture, enemy};
            if (bodies[0].id == bodies[1].id)
                bodies[1].id = bodies[0].id == 1 ? 2 : 1;
            simulateBodies(bodies, arena, step);
            for (auto& body : bodies)
                if (body.id != self.id)
                    enemy = body;
                else
                    ownFuture = body;
        }
    }
    return bestDamage >= 12 ? best : std::nullopt;
}
} // namespace

std::optional<BotAim> botAim(const Player& self, const BotTarget& target, const Arena& arena, float error,
                             bool alternatives)
{
    const auto origin = self.weaponOrigin();
    auto opponent = predictedTarget(target);
    opponent.id = target.id;
    if (meleeWeapon(self.weapon.kind))
    {
        auto at = center(target);
        float angle = std::atan2(at.y - origin.y, at.x - origin.x) + error;
        Vec2 direction{std::cos(angle), std::sin(angle)};
        auto contact = meleeContact(origin, direction, attackStats(self.weapon.kind, self.abilities).reach,
                                    opponent, arena, attackStats(self.weapon.kind, self.abilities).halfAngle);
        if (!contact)
            return {};
        // Aim at the contacted skin, including an opponent standing on our head.
        angle = std::atan2(contact->point.y - origin.y, contact->point.x - origin.x) + error;
        direction = {std::cos(angle), std::sin(angle)};
        if (!meleeContact(origin, direction, attackStats(self.weapon.kind, self.abilities).reach, opponent,
                          arena, attackStats(self.weapon.kind, self.abilities).halfAngle))
            return {};
        return BotAim{direction, 0,
                      static_cast<float>(healthDamage(self.weapon.damage, self.abilities.power(), opponent,
                                                      self.weapon.kind))};
    }
    if (self.weapon.kind == WeaponKind::Beam)
    {
        auto point = center(target);
        Vec2 delta{point.x - origin.x, point.y - origin.y};
        float n = std::max(.01f, std::hypot(delta.x, delta.y));
        if (n > 560 * self.abilities.range() || terrainHit(origin, point, arena, 0) <= 1)
            return {};
        float a = std::atan2(delta.y, delta.x) + error;
        return BotAim{{std::cos(a), std::sin(a)}, 0, float(self.weapon.damage)};
    }
    if (self.weapon.kind == WeaponKind::StickyLauncher)
    {
        if (distance(origin, center(target)) < 200)
            return {};
        auto direction = aimAtPoint(self, center(target));
        if (!direction || !clearShot(self, direction->direction, direction->flightTime, arena))
            return {};
        float a = std::atan2(direction->direction.y, direction->direction.x) + error;
        return BotAim{{std::cos(a), std::sin(a)}, direction->flightTime, 55};
    }
    if (self.weapon.kind == WeaponKind::Electric)
    {
        std::vector<Vec2> candidates{center(target)};
        if (alternatives)
        {
            std::vector<Vec2> metal;
            for (const auto& cell : arena.terrain)
                if (cell.health && cell.material == Material::Metal)
                    metal.push_back({cell.bounds.position.x + cell.bounds.size.x / 2,
                                     cell.bounds.position.y + cell.bounds.size.y / 2});
            std::stable_sort(metal.begin(), metal.end(), [&](auto a, auto b)
                             { return distance(a, center(target)) < distance(b, center(target)); });
            for (unsigned i = 0; i < std::min<std::size_t>(8, metal.size()); ++i)
                candidates.push_back(metal[i]);
        }
        std::optional<BotAim> best;
        for (auto at : candidates)
        {
            float angle = std::atan2(at.y - origin.y, at.x - origin.x);
            Vec2 dir{std::cos(angle), std::sin(angle)};
            for (const auto& contact : electricContacts(self.shotPrototype(dir), arena, {self, opponent}))
                if (contact.target == opponent.id)
                {
                    float damage = static_cast<float>(
                        healthDamage(contact.damage, self.abilities.power(), opponent, self.weapon.kind));
                    if (!best || damage > best->expectedDamage)
                        best = BotAim{{std::cos(angle + error), std::sin(angle + error)}, 0, damage};
                }
        }
        return best;
    }
    if (self.weapon.kind == WeaponKind::GrenadeLauncher)
        return grenadeAim(self, target, arena, error);
    if (self.weapon.kind == WeaponKind::Rpg)
        return rocketAim(self, target, arena, error);
    float flight =
        std::max(.001f, (distance(origin, center(target)) - self.weapon.barrel()) / shotSpeed(self));
    std::optional<BotAim> trajectory;
    for (int iteration = 0; iteration < 6; ++iteration)
    {
        opponent = predictBotTarget(target, self, flight, arena);
        trajectory = aimAtPoint(
            self, {opponent.position.x + opponent.size.x / 2, opponent.position.y + opponent.size.y / 2});
        if (!trajectory)
            return {};
        float previous = flight;
        flight = trajectory->flightTime;
        if (std::abs(flight - previous) < step / 4)
            break;
    }
    float angle = std::atan2(trajectory->direction.y, trajectory->direction.x);
    Vec2 ideal = trajectory->direction;
    Vec2 dir{std::cos(angle + error), std::sin(angle + error)};
    float damage = projectileHit(self, opponent, ideal, arena, std::min(2.f, flight + .12f));
    if (damage > 0)
        return BotAim{dir, flight, damage};
    if (!alternatives || !self.abilities.level(AbilityKind::Ricochet) || !ballisticWeapon(self.weapon.kind))
        return {};
    // Reflect towards the target's image in nearby faces; verify each real bounced path.
    unsigned attempts = 0;
    for (const auto& wall : arena.platforms)
    {
        if (++attempts > 8)
            break;
        for (unsigned side = 0; side < 4; ++side)
        {
            Vec2 reflected{opponent.position.x + opponent.size.x / 2,
                           opponent.position.y + opponent.size.y / 2};
            float plane = side < 2 ? wall.position.x + (side ? wall.size.x : 0)
                                   : wall.position.y + (side == 3 ? wall.size.y : 0);
            if (side < 2)
                reflected.x = 2 * plane - reflected.x;
            else
                reflected.y = 2 * plane - reflected.y;
            float a = std::atan2(reflected.y - origin.y, reflected.x - origin.x) + error;
            Vec2 candidate{std::cos(a), std::sin(a)};
            float time = std::min(2.f, distance(origin, reflected) / shotSpeed(self) + .15f);
            float hit = projectileHit(self, opponent, candidate, arena, time);
            if (hit > 0)
                return BotAim{candidate, time, hit};
        }
    }
    return {};
}

BotWeaponProfile botWeaponProfile(WeaponKind weapon)
{
    static constexpr std::array<BotWeaponProfile, weaponCount> profiles{
        {{220, 480}, {65, 180},  {230, 650}, {100, 270}, {300, 680}, {400, 1050}, {500, 1500},
         {170, 390}, {240, 650}, {120, 330}, {35, 84},   {35, 92},   {22, 53},    {140, 280},
         {240, 470}, {300, 950}, {180, 560}, {180, 480}, {180, 650}, {40, 110},   {240, 470}}};
    return profiles[validWeapon(weapon) ? static_cast<unsigned>(weapon) : 0];
}

} // namespace cubeshot
