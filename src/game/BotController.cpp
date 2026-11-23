#include "game/BotController.h"
#include "game/Collision.h"
#include "game/ProjectileMotion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cubeshot
{
namespace
{
constexpr float step = 1.f / 120, pi = 3.14159265358979323846f;

float distance(Vec2 a, Vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

Vec2 center(const BotTarget& p)
{
    return {p.position.x + p.size.x / 2, p.position.y + p.size.y / 2};
}

Player predictedTarget(const BotTarget& target)
{
    Player p;
    p.setAbilities(target.abilities, false);
    p.position = target.position;
    p.velocity = target.velocity;
    p.pushX = target.pushX;
    p.size = target.size;
    p.health = target.health;
    p.shields = target.shields;
    p.jetpackFuel = target.jetpackFuel;
    PlayerInput input;
    input.moveDirection = (target.velocity.x - target.pushX) / p.movementSpeed();
    input.aimDirection = target.aim;
    input.jetpackHeld = target.jetpackActive;
    p.applyInput(input);
    return p;
}

float shotSpeed(const Player& self)
{
    return self.weapon.bulletSpeed * ((ballisticWeapon(self.weapon.kind) || explosiveWeapon(self.weapon.kind))
                                          ? self.abilities.projectileSpeed()
                                          : 1.f);
}

BotWeaponProfile abilityProfile(WeaponKind kind, const Abilities& abilities)
{
    auto profile = botWeaponProfile(kind);
    if (explosiveWeapon(kind))
    {
        profile.minimum *= abilities.blastRadius();
        profile.maximum *= abilities.projectileSpeed();
    }
    else if (!meleeWeapon(kind) && kind != WeaponKind::FireBoomerang)
    {
        profile.minimum *= abilities.range();
        profile.maximum *= abilities.range();
    }
    return profile;
}

bool clearShot(const Player& self, Vec2 direction, float time, const Arena& arena)
{
    Vec2 origin = self.weaponOrigin();
    Vec2 muzzle{origin.x + direction.x * self.weapon.barrel(), origin.y + direction.y * self.weapon.barrel()};
    const float radius = Bullet::radius * (self.abilities.level(AbilityKind::LargeBullets) ? 1.5f : 1.f);
    for (const auto& r : arena.platforms)
        if (collision::segmentHit(origin, muzzle, r, radius))
            return false;
    Bullet b;
    b.collisionRadius = Bullet::radius * (self.abilities.level(AbilityKind::LargeBullets) ? 1.5f : 1.f);
    b.position = muzzle;
    b.weapon = self.weapon.kind;
    b.velocity = {direction.x * shotSpeed(self), direction.y * shotSpeed(self)};
    b.acceleration = weaponDefinition(self.weapon.kind).gravity;
    b.lifetime = weaponDefinition(self.weapon.kind).lifetime *
                 (self.weapon.kind == WeaponKind::Flamethrower ? self.abilities.range() : 1.f);
    b.rangeScale = self.abilities.range();
    for (float t = 0; t < time; t += step)
    {
        auto start = b.position;
        b.update(std::min(step, time - t));
        if (b.lifetime < -1e-4f)
            return false;
        for (const auto& r : arena.platforms)
            if (collision::segmentHit(start, b.position, r, b.collisionRadius))
                return false;
    }
    return true;
}

bool outside(Vec2 p, const Arena& arena)
{
    const auto& b = arena.bounds;
    return p.x < b.position.x + Bullet::radius || p.y < b.position.y + Bullet::radius ||
           p.x > b.position.x + b.size.x - Bullet::radius || p.y > b.position.y + b.size.y - Bullet::radius;
}

float terrainHit(Vec2 from, Vec2 to, const Arena& arena, float radius = Bullet::radius)
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
        const auto start = bullet.position;
        if (bullet.weapon == WeaponKind::FireBoomerang)
        {
            if (bullet.age >= .55f)
                bullet.returning = true;
            if (bullet.returning)
            {
                float d = distance(bullet.position, owner);
                if (d < 24)
                    return false;
                bullet.velocity = {(owner.x - bullet.position.x) / d * 700,
                                   (owner.y - bullet.position.y) / d * 700};
            }
        }
        if (bullet.weapon != WeaponKind::FireBoomerang)
        {
            if (!advanceProjectileTerrain(bullet, arena, std::min(step, age - elapsed)))
                return false;
            continue;
        }
        bullet.update(std::min(step, age - elapsed));
        const float obstacle = terrainHit(start, bullet.position, arena, bullet.collisionRadius);
        if (bullet.lifetime <= 0)
            return false;
        if (bullet.weapon == WeaponKind::FireBoomerang && (obstacle <= 1 || outside(bullet.position, arena)))
        {
            bullet.returning = true;
            bullet.position = obstacle <= 1 ? Vec2{start.x + (bullet.position.x - start.x) * obstacle,
                                                   start.y + (bullet.position.y - start.y) * obstacle}
                                            : start;
        }
        else if (obstacle <= 1 || outside(bullet.position, arena))
            return false;
    }
    return bullet.lifetime > 0;
}

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
    std::array<FlightStep, dodgeSteps> path;
    std::size_t count = 0;
    WeaponKind weapon = WeaponKind::Pistol;
    int damage = 20;
    float blastRadius = 240, power = 1;
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
    out.weapon = bullet.weapon;
    out.damage = bullet.damage;
    out.blastRadius = (bullet.weapon == WeaponKind::GrenadeLauncher ? 180.f : 240.f) * bullet.blastScale;
    out.power = bullet.power;
    for (float elapsed = 0; elapsed < horizon && out.count < dodgeSteps; elapsed += step)
    {
        const auto from = bullet.position;
        if (bullet.weapon == WeaponKind::FireBoomerang)
        {
            if (bullet.age >= .55f)
                bullet.returning = true;
            if (bullet.returning)
            {
                float d = distance(bullet.position, owner);
                if (d < 24)
                    break;
                bullet.velocity = {(owner.x - bullet.position.x) / d * 700,
                                   (owner.y - bullet.position.y) / d * 700};
            }
        }
        if (bullet.weapon != WeaponKind::FireBoomerang)
        {
            const float dt = std::min(step, horizon - elapsed);
            if (bullet.lifetime <= dt)
            {
                if (explosiveWeapon(bullet.weapon))
                    out.path[out.count++] = {from, from, 1,
                                             static_cast<int>(std::lround(bullet.damage * bullet.power)),
                                             bullet.collisionRadius};
                break;
            }
            bool active = advanceProjectileTerrain(bullet, arena, dt);
            out.path[out.count++] = {from, bullet.position, active ? 2.f : 1.f,
                                     static_cast<int>(std::lround(bullet.impactDamage() * bullet.power)),
                                     bullet.collisionRadius};
            if (!active)
                break;
            continue;
        }
        bullet.update(std::min(step, horizon - elapsed));
        if (bullet.lifetime <= 0)
            break;
        const float barrier = terrainHit(from, bullet.position, arena, bullet.collisionRadius);
        out.path[out.count++] = {from, bullet.position, barrier, bullet.impactDamage(),
                                 bullet.collisionRadius};
        if (barrier <= 1 && bullet.weapon == WeaponKind::FireBoomerang)
        {
            bullet.position = from;
            bullet.returning = true;
        }
        else if (barrier <= 1 || outside(bullet.position, arena))
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

float expectedDamage(const Threat& threat, const BodyPath& body, const Arena& arena)
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
            return static_cast<float>(flight.damage) + (incendiaryWeapon(threat.weapon) ? 12.f : 0.f);
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
                damage = std::max(
                    damage, (threat.weapon == WeaponKind::GrenadeLauncher ? 90.f : 120.f) * threat.power *
                                std::max(0.f, 1 - distance(blast, nearest) / threat.blastRadius));
        }
    }
    return damage;
}

std::optional<BotAim> rocketAim(const Player& self, const BotTarget& target, const Arena& arena, float error)
{
    const auto origin = self.weaponOrigin();
    auto nearest = [](Vec2 point, Rect box)
    {
        return Vec2{std::clamp(point.x, box.position.x, box.position.x + box.size.x),
                    std::clamp(point.y, box.position.y, box.position.y + box.size.y)};
    };
    const float radius = explosionRadius * self.abilities.blastRadius();
    if (distance(origin, center(target)) < radius + 30)
        return {};
    std::optional<BotAim> best;
    float bestDamage = 0;
    for (auto offset : {Vec2{0, -24}, Vec2{0, -3}, Vec2{-65, -3}, Vec2{65, -3}})
    {
        float flight =
            std::max(.01f, (distance(origin, center(target)) - self.weapon.barrel()) / (shotSpeed(self)));
        Vec2 delta;
        Player future;
        future.setAbilities(target.abilities, false);
        future.position = target.position;
        future.velocity = target.velocity;
        future.size = target.size;
        for (int iteration = 0; iteration < 5; ++iteration)
        {
            future = predictedTarget(target);
            future.position = target.position;
            future.velocity = target.velocity;
            future.pushX = target.pushX;
            future.update(flight, arena);
            delta = {future.position.x + target.size.x / 2 + offset.x - origin.x,
                     future.position.y + target.size.y + offset.y - origin.y - 25 * flight * flight / 2};
            flight =
                std::max(.01f, (std::hypot(delta.x, delta.y) - self.weapon.barrel()) / (shotSpeed(self)));
        }
        if (flight > 4)
            continue;
        const float angle = std::atan2(delta.y, delta.x) + error;
        Vec2 direction{std::cos(angle), std::sin(angle)};
        Bullet rocket;
        rocket.position = {origin.x + direction.x * self.weapon.barrel(),
                           origin.y + direction.y * self.weapon.barrel()};
        rocket.velocity = {direction.x * shotSpeed(self), direction.y * shotSpeed(self)};
        rocket.acceleration = 25;
        rocket.lifetime = weaponDefinition(WeaponKind::Rpg).lifetime;
        rocket.weapon = WeaponKind::Rpg;
        rocket.collisionRadius =
            Bullet::radius * (self.abilities.level(AbilityKind::LargeBullets) ? 1.5f : 1.f);
        if (terrainHit(origin, rocket.position, arena, rocket.collisionRadius) <= 1)
            continue;
        for (float elapsed = 0; elapsed < flight + .2f; elapsed += 1.f / 60)
        {
            auto from = rocket.position;
            rocket.update(1.f / 60);
            float hit = terrainHit(from, rocket.position, arena, rocket.collisionRadius);
            if (auto playerHit = collision::segmentHit(from, rocket.position, {future.position, future.size},
                                                       Bullet::radius))
                hit = std::min(hit, *playerHit);
            if (hit > 1)
                continue;
            const Vec2 blast{from.x + (rocket.position.x - from.x) * hit,
                             from.y + (rocket.position.y - from.y) * hit};
            const Vec2 victim = nearest(blast, {future.position, future.size});
            bool blocked = false;
            for (const auto& wall : arena.platforms)
                if (auto t = collision::segmentHit(blast, victim, wall, 0); t && *t < .98f)
                {
                    blocked = true;
                    break;
                }
            const float damage = 120 * std::max(0.f, 1 - distance(blast, victim) / radius);
            const Rect predictedSelf{
                {self.position.x + self.velocity.x * (elapsed + 1.f / 60), self.position.y}, self.size};
            if (!blocked && damage > bestDamage && damage >= 12 &&
                distance(blast, nearest(blast, self.bounds())) > radius + 8 &&
                distance(blast, nearest(blast, predictedSelf)) > radius + 8)
            {
                bestDamage = damage;
                best = BotAim{direction, elapsed + hit / 60};
            }
            break;
        }
        if (bestDamage >= 90)
            break;
    }
    return best;
}

} // namespace

std::u32string_view botDifficultyName(BotDifficulty difficulty)
{
    switch (difficulty)
    {
        case BotDifficulty::Easy:
            return U"Лёгкая";
        case BotDifficulty::Hard:
            return U"Сложная";
        default:
            return U"Средняя";
    }
}

BotTuning botTuning(BotDifficulty difficulty)
{
    switch (difficulty)
    {
        case BotDifficulty::Easy:
            return {.5f, 8, .75f, .25f};
        case BotDifficulty::Hard:
            return {.1f, .75f, .25f, .65f};
        default:
            return {.3f, 4, .45f, .45f};
    }
}

BotObservation observeBot(const World& world, PlayerId self, PlayerId opponent)
{
    BotObservation result;
    result.geometry = &world.arena();
    for (const auto& p : world.players())
    {
        if (p.id == self)
            result.self = p;
        if (p.id == opponent && p.alive())
            result.target = BotTarget{p.position,     p.velocity,         p.size,
                                      p.grounded,     p.weapon.kind,      p.aimDirection,
                                      p.health,       p.weapon.charging,  p.weapon.overheated,
                                      p.weapon.shots, p.weapon.reloading, p.pushX,
                                      p.abilities,    p.shields,          p.jetpackFuel,
                                      p.jetpackActive};
    }
    for (const auto& b : world.bullets())
        if (b.shooterId != self || explosiveWeapon(b.weapon))
            result.bullets.push_back(b);
    result.fires = world.fires();
    return result;
}

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
        Bullet b;
        b.weapon = WeaponKind::GrenadeLauncher;
        b.position = self.weaponOrigin();
        b.velocity = {direction.x * 420 * self.abilities.projectileSpeed(),
                      direction.y * 420 * self.abilities.projectileSpeed()};
        b.acceleration = 900;
        b.lifetime = 1.5f;
        Player enemy = predictedTarget(target);
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
                auto nearestPoint = [](Vec2 p, Rect r)
                {
                    return Vec2{std::clamp(p.x, r.position.x, r.position.x + r.size.x),
                                std::clamp(p.y, r.position.y, r.position.y + r.size.y)};
                };
                float ownDistance = distance(b.position, nearestPoint(b.position, self.bounds()));
                float targetDistance = distance(b.position, nearestPoint(b.position, enemy.bounds()));
                if (ownDistance > radius + 12 &&
                    (direct ||
                     terrainHit(b.position, nearestPoint(b.position, enemy.bounds()), arena, 0) > 1))
                {
                    float damage = direct ? 90.f : 90 * std::max(0.f, 1 - targetDistance / radius);
                    if (damage > bestDamage + 1)
                    {
                        bestDamage = damage;
                        best = BotAim{direction, (tick + 1) * step};
                    }
                }
                break;
            }
            enemy.update(step, arena);
        }
    }
    return bestDamage >= 12 ? best : std::nullopt;
}
} // namespace

std::optional<BotAim> botAim(const Player& self, const BotTarget& target, const Arena& arena, float error)
{
    const auto origin = self.weaponOrigin();
    if (self.weapon.kind == WeaponKind::GrenadeLauncher)
        return grenadeAim(self, target, arena, error);
    if (meleeWeapon(self.weapon.kind))
    {
        const Vec2 delta{center(target).x - origin.x, center(target).y - origin.y};
        const Vec2 nearest{std::clamp(origin.x, target.position.x, target.position.x + target.size.x),
                           std::clamp(origin.y, target.position.y, target.position.y + target.size.y)};
        if (distance(origin, nearest) > self.weapon.barrel())
            return {};
        const float angle = std::atan2(delta.y, delta.x) + error;
        const Vec2 direction{std::cos(angle), std::sin(angle)};
        for (const auto& wall : arena.platforms)
            if (collision::segmentHit(origin, nearest, wall, 0))
                return {};
        return BotAim{direction, 0};
    }
    if (self.weapon.kind == WeaponKind::Electric)
    {
        auto at = center(target);
        Vec2 delta{at.x - origin.x, at.y - origin.y};
        float length = std::hypot(delta.x, delta.y);
        if (length > 600 * self.abilities.range())
            return {};
        for (auto& wall : arena.platforms)
            if (collision::segmentHit(origin, at, wall, 0))
                return {};
        return BotAim{{delta.x / std::max(length, .01f), delta.y / std::max(length, .01f)}, 0};
    }
    if (self.weapon.kind == WeaponKind::Rpg)
        return rocketAim(self, target, arena, error);
    float flight =
        std::max(.001f, (distance(origin, center(target)) - self.weapon.barrel()) / (shotSpeed(self)));
    Vec2 delta;
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        Player predicted = predictedTarget(target);
        predicted.position = target.position;
        predicted.velocity = target.velocity;
        predicted.pushX = target.pushX;
        predicted.size = target.size;
        predicted.update(flight, arena);
        delta = {predicted.position.x + target.size.x / 2 - origin.x,
                 predicted.position.y + target.size.y / 2 - origin.y -
                     weaponDefinition(self.weapon.kind).gravity * flight * flight / 2};
        flight = std::max(.001f, (std::hypot(delta.x, delta.y) - self.weapon.barrel()) / (shotSpeed(self)));
        if (flight > weaponDefinition(self.weapon.kind).lifetime *
                         (self.weapon.kind == WeaponKind::Flamethrower ? self.abilities.range() : 1.f) ||
            !std::isfinite(flight))
            return {};
    }
    const float angle = std::atan2(delta.y, delta.x) + error;
    Vec2 direction{std::cos(angle), std::sin(angle)};
    if (!clearShot(self, direction, flight, arena))
        return {};
    return BotAim{direction, flight};
}

BotController::BotController(Arena arena, BotDifficulty difficulty, std::uint32_t seed)
    : mobility_(arena, difficulty == BotDifficulty::Hard), mobile_(difficulty != BotDifficulty::Easy),
      hard_(difficulty == BotDifficulty::Hard), arena_(std::move(arena)), tuning_(botTuning(difficulty)),
      random_(seed), navigation_(BotNavigation::forArena(arena_))
{
    reset();
}

void BotController::reset()
{
    navigationWorker_.cancel();
    mobility_.reset();
    movementAbilities_.reset();
    history_.clear();
    perceived_ = {};
    time_ = nextShot_ = nextAim_ = replan_ = stuck_ = avoidedUntil_ = nextGoal_ = 0;
    error_ = 0;
    aim_ = {-1, 0};
    path_.clear();
    pathIndex_ = 0;
    traversalTick_ = avoidFrom_ = avoidTo_ = -1;
    progress_ = goal_ = {};
    leftGround_ = false;
    dodgeUntil_ = dodgeMove_ = nextDodgeJump_ = 0;
    jumpedBullets_.clear();
    enemyShots_ = 0;
    enemyCharging_ = false;
    enemyShotAt_ = -100;
    coverSince_ = -1;
    attackUntil_ = 0;
    lastWeapon_ = enemyWeapon_ = WeaponKind::Pistol;
    routeHazards_.clear();
}

BotWeaponProfile botWeaponProfile(WeaponKind weapon)
{
    static constexpr std::array<BotWeaponProfile, weaponCount> profiles{{{220, 480},
                                                                         {65, 180},
                                                                         {230, 650},
                                                                         {100, 270},
                                                                         {300, 680},
                                                                         {400, 1050},
                                                                         {500, 1500},
                                                                         {170, 390},
                                                                         {240, 650},
                                                                         {120, 330},
                                                                         {35, 84},
                                                                         {35, 92},
                                                                         {22, 53},
                                                                         {140, 280},
                                                                         {240, 470},
                                                                         {300, 950},
                                                                         {180, 560}}};
    return profiles[validWeapon(weapon) ? static_cast<unsigned>(weapon) : 0];
}

float BotController::hazardCost(Vec2 position) const
{
    float cost = 0;
    for (const auto& fire : perceived_.fires)
    {
        const float dx = std::max(0.f, std::abs(position.x + 24 - fire.position.x) - 24);
        const float dy = std::max(0.f, std::abs(position.y + 24 - fire.position.y) - 24);
        const float distance = std::hypot(dx, dy);
        if (distance < fire.radius + 12)
            cost += 1.f + (fire.radius + 12 - distance) / 20;
    }
    return cost;
}

Vec2 BotController::chooseGoal(const Player& self, const std::optional<BotTarget>& target)
{
    if (!target)
        return self.position;
    auto profile = abilityProfile(self.weapon.kind, self.abilities);
    const auto enemy = abilityProfile(target->weapon, target->abilities);
    const bool close = meleeWeapon(self.weapon.kind) || self.weapon.kind == WeaponKind::Shotgun ||
                       self.weapon.kind == WeaponKind::Flamethrower;
    const bool threatMelee = meleeWeapon(target->weapon);
    if (threatMelee && !meleeWeapon(self.weapon.kind))
        profile.minimum = std::max(profile.minimum, enemy.maximum + 85);
    const float enemyInterval = weaponDefinition(target->weapon).interval / target->abilities.fireRate();
    const bool enemyReady = !target->reloading && !target->overheated &&
                            (time_ - enemyShotAt_ > enemyInterval || target->weapon != enemyWeapon_);
    // Use an actual range advantage against short-range firearms as well as melee.
    // During an observed overheat/recovery window it is safe to keep closing.
    if (enemyReady && !threatMelee && !meleeWeapon(self.weapon.kind) && profile.maximum > enemy.maximum + 60)
        profile.minimum = std::max(profile.minimum, std::min(enemy.maximum + 45, profile.maximum - 20));
    const bool lethal = (target->weapon == WeaponKind::Sniper || target->weapon == WeaponKind::Rifle) &&
                        self.shields == 0 &&
                        self.health <= weaponDefinition(target->weapon).damage * target->abilities.power() *
                                           (1 - .1f * self.abilities.level(AbilityKind::ToughSkin));
    bool covered = false;
    for (const auto& wall : arena_.platforms)
        if (collision::segmentHit(center(*target), self.weaponOrigin(), wall, 3))
        {
            covered = true;
            break;
        }
    if (covered)
    {
        if (coverSince_ < 0)
            coverSince_ = time_;
        if (time_ - coverSince_ > (hard_ ? .35f : .7f) && self.weapon.cooldown <= 0 &&
            !self.weapon.overheated && !target->charging)
        {
            attackUntil_ = time_ + .9f;
            coverSince_ = -1;
        }
    }
    else
        coverSince_ = -1;
    const bool wantsCover =
        ((lethal && enemyReady) || self.weapon.overheated || self.weapon.reloading) && time_ >= attackUntil_;
    auto exposure = [&](Vec2 at)
    {
        const auto start = center(*target);
        const Vec2 finish{at.x + 24, at.y + 24};
        for (const auto& box : arena_.platforms)
            if (collision::segmentHit(start, finish, box, 3))
                return 0.f;
        const float distanceToEnemy = distance(start, finish);
        if (target->charging && enemyReady)
            return 950.f;
        if (wantsCover)
            return (close ? 400.f : 240.f);
        if (threatMelee && !meleeWeapon(self.weapon.kind) && distanceToEnemy < enemy.maximum + 60)
            return 650.f;
        return 0.f;
    };
    auto baseCost = [&](Vec2 at)
    {
        const float range = distance({at.x + 24, at.y + 26}, center(*target));
        float out = std::max({profile.minimum - range, range - profile.maximum, 0.f});
        out *= threatMelee && !meleeWeapon(self.weapon.kind) ? 3.f : 1.3f;
        // A close-range fighter must keep closing during the sniper's recovery window.
        if (close && lethal && !enemyReady)
            out += std::max(0.f, range - profile.maximum) * .35f;
        return out + distance(at, self.position) * .18f + hazardCost(at) * 700 + exposure(at);
    };
    const float currentRange = distance(self.weaponOrigin(), center(*target));
    if (currentRange >= profile.minimum && currentRange <= profile.maximum &&
        hazardCost(self.position) == 0 && exposure(self.position) == 0 && botAim(self, *target, arena_))
        return self.position;

    struct Candidate
    {
        float cost;
        std::size_t index;
    };

    const auto& nodes = navigation_->nodes();
    std::vector<Candidate> candidates;
    candidates.reserve(nodes.size());
    routeHazards_.clear();
    routeHazards_.reserve(nodes.size());
    for (std::size_t n = 0; n < nodes.size(); ++n)
    {
        candidates.push_back({baseCost(nodes[n].position), n});
        routeHazards_.push_back(hazardCost(nodes[n].position) * 8);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](auto a, auto b) { return a.cost < b.cost; });
    float best = std::numeric_limits<float>::max();
    Vec2 goal = self.position;
    for (const auto& item : candidates)
    {
        if (item.cost > best)
            break;
        Player candidate = self;
        candidate.position = nodes[item.index].position;
        bool canAttack = false;
        if (candidate.weapon.kind == WeaponKind::Rpg)
        {
            const auto origin = candidate.weaponOrigin(), at = center(*target);
            const float flight =
                std::max(.01f, (distance(origin, at) - candidate.weapon.barrel()) / (shotSpeed(self)));
            const float angle = std::atan2(at.y - origin.y - 12.5f * flight * flight, at.x - origin.x);
            canAttack = flight <= weaponDefinition(WeaponKind::Rpg).lifetime &&
                        clearShot(candidate, {std::cos(angle), std::sin(angle)}, flight, arena_);
        }
        else if (candidate.weapon.kind == WeaponKind::GrenadeLauncher)
            canAttack = distance(candidate.position, target->position) > 200 &&
                        distance(candidate.position, target->position) < 500;
        else
            canAttack = botAim(candidate, *target, arena_).has_value();
        const float cost = item.cost + (canAttack ? 0.f : wantsCover ? 140.f : 450.f);
        if (cost >= best)
            continue;
        if (navigation_->nearest(self.position) != item.index &&
            navigation_->route(self.position, candidate.position, -1, -1, routeHazards_).empty())
        {
            const bool boost =
                mobile_ && (self.weapon.kind == WeaponKind::Shotgun ||
                            self.weapon.kind == WeaponKind::Minigun || self.weapon.kind == WeaponKind::Rpg);
            float rise = self.weapon.kind == WeaponKind::Minigun   ? 650.f
                         : self.weapon.kind == WeaponKind::Shotgun ? 320.f
                                                                   : 230.f;
            if (!boost || candidate.position.y < self.position.y - rise ||
                distance(candidate.position, self.position) > 800)
                continue;
        }
        best = cost;
        goal = candidate.position;
    }
    return goal;
}

void BotController::cancelRoute(bool failed)
{
    if (failed && pathIndex_ < path_.size())
    {
        avoidFrom_ = static_cast<int>(path_[pathIndex_].from);
        avoidTo_ = static_cast<int>(path_[pathIndex_].to);
        avoidedUntil_ = time_ + 2;
    }
    path_.clear();
    pathIndex_ = 0;
    traversalTick_ = -1;
    leftGround_ = false;
    stuck_ = 0;
    replan_ = 0;
}

PlayerInput BotController::navigate(const Player& self, Vec2 goal, float dt)
{
    PlayerInput input;
    const auto& nodes = navigation_->nodes();
    if (nodes.empty())
        return input;
    if (distance(self.position, progress_) >= 8)
    {
        progress_ = self.position;
        stuck_ = 0;
    }
    else if (pathIndex_ < path_.size())
        stuck_ += dt;
    if (stuck_ >= 1)
        cancelRoute(true);
    if (time_ >= replan_ && self.grounded && traversalTick_ < 0)
    {
        path_ = navigation_->route(self.position, goal, time_ < avoidedUntil_ ? avoidFrom_ : -1,
                                   time_ < avoidedUntil_ ? avoidTo_ : -1, routeHazards_);
        pathIndex_ = 0;
        replan_ = time_ + .6f;
    }
    if (pathIndex_ >= path_.size())
    {
        path_.clear();
        pathIndex_ = 0;
        if (self.grounded && std::abs(goal.y - self.position.y) < 2 &&
            navigation_->nearest(self.position) == navigation_->nearest(goal))
            input.moveDirection =
                std::clamp((goal.x - self.position.x) / (self.movementSpeed() * step), -1.f, 1.f);
        else if (!self.grounded)
            input.moveDirection = self.velocity.x / self.movementSpeed();
        return input;
    }
    const auto& edge = path_[pathIndex_];
    const Vec2 start = nodes[edge.from].position, end = nodes[edge.to].position;
    if (!edge.jump && nodes[edge.from].surface == nodes[edge.to].surface)
    {
        if (!self.grounded || std::abs(self.position.y - end.y) > 2)
        {
            cancelRoute(true);
            input.moveDirection = goal.x > self.position.x ? 1.f : -1.f;
            return input;
        }
        const float dx = end.x - self.position.x;
        if (std::abs(dx) <= .05f)
            ++pathIndex_;
        else
            input.moveDirection = std::clamp(dx / (self.movementSpeed() * step), -1.f, 1.f);
        return input;
    }
    if (traversalTick_ < 0)
    {
        const float dx = start.x - self.position.x;
        if (std::abs(dx) > .05f || std::abs(start.y - self.position.y) > 2 || !self.grounded)
        {
            input.moveDirection = std::clamp(dx / (self.movementSpeed() * step), -1.f, 1.f);
            return input;
        }
        traversalTick_ = 1;
        leftGround_ = false;
        input.jump = edge.jump;
        input.jetpackHeld = edge.jetpack;
        input.moveDirection = static_cast<float>(edge.direction);
        return input;
    }
    leftGround_ = leftGround_ || !self.grounded;
    if (self.grounded && (leftGround_ || (edge.jump && traversalTick_ >= 4)))
    {
        if (std::abs(self.position.y - end.y) > 2 || std::abs(self.position.x - end.x) > 35)
            cancelRoute(true);
        else
        {
            traversalTick_ = -1;
            leftGround_ = false;
            ++pathIndex_;
        }
        return input;
    }
    if (traversalTick_ > edge.durationTicks + 45)
    {
        cancelRoute(true);
        return input;
    }
    const float baselineSpeed = Player::moveSpeed * self.abilities.speed();
    const int hold = static_cast<int>(std::lround(edge.holdTicks * baselineSpeed / self.movementSpeed()));
    input.moveDirection = traversalTick_ < hold ? static_cast<float>(edge.direction) : 0.f;
    input.jump = traversalTick_ == edge.secondJumpTick;
    input.jetpackHeld = edge.jetpack && traversalTick_ < edge.holdTicks;
    ++traversalTick_;
    return input;
}

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
            hits += expectedDamage(b, body, arena_);
        for (std::size_t n = 0; n <= body.count; n += 8)
            hits += hazardCost(body.points[n]) * 6;
        return hits;
    };
    if (known.empty() && perceived_.fires.empty())
        return;
    const auto initialBody = predictBody(observation.self, input, tuning_.dodgeHorizon, arena_);
    float initialRisk = risk(input);
    bool freshThreat = false;
    for (const auto& b : known)
        if (expectedDamage(b, initialBody, arena_) > 0)
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

void BotController::prepare(const Abilities& abilities, const Arena* arena)
{
    if (arena && (arena_.map != arena->map || arena_.terrainRevision != arena->terrainRevision))
    {
        arena_ = *arena;
        movementAbilities_.reset();
    }
    if (!movementAbilities_ || *movementAbilities_ != abilities)
    {
        navigation_ = BotNavigation::forArena(arena_, abilities);
        movementAbilities_ = abilities;
        mobility_.geometry(arena_, navigation_, abilities);
        cancelRoute(false);
    }
}

PlayerInput BotController::update(const BotObservation& observation, float dt)
{
    prepare(observation.self.abilities);
    if (observation.geometry && (arena_.map != observation.geometry->map ||
                                 arena_.terrainRevision != observation.geometry->terrainRevision))
    {
        bool topology = arena_.map != observation.geometry->map ||
                        arena_.geometryRevision != observation.geometry->geometryRevision;
        arena_ = *observation.geometry;
        if (topology)
        {
            cancelRoute(false);
            navigationGeneration_ = navigationWorker_.request(arena_, observation.self.abilities);
            mobility_.geometry(arena_, navigation_, observation.self.abilities);
        }
    }
    if (auto ready = navigationWorker_.take(navigationGeneration_))
    {
        navigation_ = std::move(ready);
        cancelRoute(false);
        mobility_.geometry(arena_, navigation_, observation.self.abilities);
    }

    if (!(dt > 0) || !std::isfinite(dt) || !observation.self.alive())
        return {};
    if (time_ == 0)
        aim_ = observation.self.aimDirection;
    time_ += dt;
    const bool previouslyKnown = perceived_.target.has_value();
    history_.push_back({time_, observation.target, observation.bullets, observation.fires});
    while (!history_.empty() && history_.front().time <= time_ - tuning_.reaction)
    {
        perceived_ = std::move(history_.front());
        history_.pop_front();
    }
    if (!previouslyKnown && perceived_.target)
        nextGoal_ = 0;
    if (observation.self.weapon.kind != lastWeapon_)
    {
        lastWeapon_ = observation.self.weapon.kind;
        cancelRoute(false);
        nextGoal_ = nextShot_ = 0;
    }
    if (perceived_.target)
    {
        if (perceived_.target->weapon != enemyWeapon_)
        {
            enemyWeapon_ = perceived_.target->weapon;
            enemyShots_ = perceived_.target->shots;
            enemyShotAt_ = -100;
            nextGoal_ = 0;
        }
        else if (perceived_.target->shots != enemyShots_)
        {
            enemyShots_ = perceived_.target->shots;
            enemyShotAt_ = perceived_.time;
            nextGoal_ = 0;
        }
        if (perceived_.target->charging != enemyCharging_)
        {
            enemyCharging_ = perceived_.target->charging;
            nextGoal_ = 0;
        }
    }
    // Goals must be stable support positions. A cached mid-jump position can otherwise
    // map to a nearby platform and cause another jump after the threat has passed.
    if (time_ >= nextGoal_ && observation.self.grounded && time_ >= dodgeUntil_)
    {
        const auto next = chooseGoal(observation.self, perceived_.target);
        if (distance(next, goal_) > 12)
            cancelRoute(false);
        goal_ = next;
        nextGoal_ = time_ + (hard_ ? .22f : .4f);
    }
    PlayerInput input;
    if (time_ < dodgeUntil_)
        input.moveDirection = dodgeMove_;
    else
        input = navigate(observation.self, goal_, dt);
    avoidBullets(observation, input);
    if (time_ >= nextAim_)
    {
        error_ = std::uniform_real_distribution<float>(-tuning_.aimErrorDegrees,
                                                       tuning_.aimErrorDegrees)(random_) *
                 pi / 180;
        nextAim_ = time_ + .4f;
    }
    bool alignedFire = false;
    if (perceived_.target)
    {
        // Extrapolate the delayed observation, never the opponent's unobserved input.
        auto estimate = *perceived_.target;
        Player predicted = predictedTarget(estimate);
        predicted.position = estimate.position;
        predicted.velocity = estimate.velocity;
        predicted.pushX = estimate.pushX;
        predicted.size = estimate.size;
        predicted.update(std::max(0.f, time_ - perceived_.time), arena_);
        estimate.position = predicted.position;
        estimate.velocity = predicted.velocity;
        estimate.grounded = predicted.grounded;
        estimate.jetpackFuel = predicted.jetpackFuel;
        estimate.jetpackActive = predicted.jetpackActive;
        bool demolitionShot = false;
        auto solution = botAim(observation.self, estimate, arena_, error_);
        if (!solution && !meleeWeapon(observation.self.weapon.kind))
        {
            Vec2 origin = observation.self.weaponOrigin(), target = center(estimate),
                 delta{target.x - origin.x, target.y - origin.y};
            float length = std::max(.01f, std::hypot(delta.x, delta.y)), nearest = 2;
            Vec2 contact{};
            for (auto& wall : arena_.platforms)
                if (auto hit = collision::segmentHit(origin, target, wall, 0); hit && *hit < nearest)
                {
                    nearest = *hit;
                    contact = {origin.x + delta.x * *hit, origin.y + delta.y * *hit};
                }
            if (nearest <= 1 && arena_.terrainAt(contact, 2) &&
                (!explosiveWeapon(observation.self.weapon.kind) || length * nearest > 270))
            {
                solution = BotAim{{delta.x / length, delta.y / length}, 0};
                demolitionShot = true;
            }
        }
        if (solution)
        {
            float current = std::atan2(aim_.y, aim_.x);
            const float desired = std::atan2(solution->direction.y, solution->direction.x);
            float change = std::remainder(desired - current, 2 * pi);
            current += std::clamp(change, -(hard_ ? 10.f : 6.f) * dt, (hard_ ? 10.f : 6.f) * dt);
            aim_ = {std::cos(current), std::sin(current)};
            alignedFire =
                std::abs(change) < .06f && (demolitionShot || meleeWeapon(observation.self.weapon.kind) ||
                                            explosiveWeapon(observation.self.weapon.kind) ||
                                            clearShot(observation.self, aim_, solution->flightTime, arena_));
            if (observation.target && time_ >= nextShot_ && observation.self.weapon.cooldown <= 0 &&
                std::abs(change) < .06f &&
                (demolitionShot || meleeWeapon(observation.self.weapon.kind) ||
                 explosiveWeapon(observation.self.weapon.kind) ||
                 clearShot(observation.self, aim_, solution->flightTime, arena_)))
            {
                input.shoot = true;
                nextShot_ =
                    time_ +
                    std::max(tuning_.shotInterval * observation.self.weapon.shotInterval / .25f,
                             observation.self.weapon.shotInterval) /
                        (observation.self.abilities.fireRate() * (1 + .15f * observation.self.adrenaline()));
            }
        }
    }
    if (observation.self.weapon.charging)
        input.shoot = alignedFire && observation.target.has_value();
    if (observation.self.weapon.automatic())
        input.shoot = alignedFire && observation.target.has_value();
    input.triggerHeld = input.shoot;
    input.aimDirection = aim_;
    const auto& d = weaponDefinition(observation.self.weapon.kind);
    if (observation.self.weapon.reloading)
        input.shoot = input.triggerHeld = false;
    if (d.capacity && observation.self.weapon.ammo > 0 &&
        observation.self.weapon.ammo <= std::max(1, observation.self.weapon.capacity() / 4) &&
        perceived_.target && !botAim(observation.self, *perceived_.target, arena_) &&
        !observation.self.weapon.reloading)
        input.reload = true;
    if (observation.self.weapon.kind == WeaponKind::Minigun && observation.self.weapon.heat > .9f)
        input.shoot = input.triggerHeld = false;
    input.moveDirection =
        std::clamp(input.moveDirection - observation.self.pushX *
                                             std::exp(-(observation.self.grounded ? 10.f : 3.f) * dt) /
                                             observation.self.movementSpeed(),
                   -1.f, 1.f);
    if (mobile_ && perceived_.target)
    {
        auto known = perceived_.bullets;
        known.erase(std::remove_if(known.begin(), known.end(),
                                   [&](Bullet& b)
                                   {
                                       return !advanceKnownBullet(b, std::max(0.f, time_ - perceived_.time),
                                                                  arena_, center(*perceived_.target));
                                   }),
                    known.end());
        Player opponent = predictedTarget(*perceived_.target);
        opponent.id = observation.self.id == 1 ? 2 : 1;
        opponent.position = perceived_.target->position;
        opponent.velocity = perceived_.target->velocity;
        opponent.pushX = perceived_.target->pushX;
        opponent.update(std::max(0.f, time_ - perceived_.time), arena_);
        if (mobility_.control(observation.self, goal_, known, perceived_.fires, input, dt, &opponent))
        {
            aim_ = input.aimDirection;
            cancelRoute(false);
        }
    }
    input.jetpackHeld = input.jetpackHeld || input.jump;
    return input;
}
} // namespace cubeshot
