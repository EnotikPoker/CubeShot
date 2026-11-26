#include "game/BotController.h"
#include "game/AttackQueries.h"
#include "game/AttackStats.h"
#include "game/BodyPhysics.h"
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

BotController::BotController(Arena arena, BotDifficulty difficulty, std::uint32_t seed)
    : mobility_(arena, difficulty == BotDifficulty::Hard), mobile_(difficulty != BotDifficulty::Easy),
      hard_(difficulty == BotDifficulty::Hard), arena_(std::move(arena)), tuning_(botTuning(difficulty)),
      random_(seed), navigation_(BotNavigation::forArena(arena_))
{
    reset();
}

void BotController::reset()
{
    nextActive_ = progressBarrier_ = 0;
    intent_ = BotIntent::Approach;
    attack_.reset();
    nextAttack_ = 0;
    progressTime_ = 0;
    progressDistance_ = 1e9f;
    enemyProgressAt_ = 0;
    progressHealth_ = -1;
    progressShields_ = 0;
    progressTerrain_ = 0;
    repositionUntil_ = 0;
    lastGoal_ = {};

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

void BotController::prepare(const Abilities& abilities, const Arena* arena)
{
    if (arena && (arena_.map != arena->map || arena_.terrainRevision != arena->terrainRevision ||
                  arena_.modifiers.gravity != arena->modifiers.gravity ||
                  arena_.modifiers.movement != arena->modifiers.movement))
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
    if (observation.geometry && (arena_.modifiers.gravity != observation.geometry->modifiers.gravity ||
                                 arena_.modifiers.movement != observation.geometry->modifiers.movement))
        prepare(observation.self.abilities, observation.geometry);
    prepare(observation.self.abilities);
    if (observation.geometry)
        arena_.combatScreens = observation.geometry->combatScreens;
    if (observation.geometry && (arena_.map != observation.geometry->map ||
                                 arena_.terrainRevision != observation.geometry->terrainRevision))
    {
        bool topology = arena_.map != observation.geometry->map ||
                        arena_.geometryRevision != observation.geometry->geometryRevision;
        arena_ = *observation.geometry;
        if (topology)
        {
            cancelRoute(false);
            nextGoal_ = nextAttack_ = 0;
            attack_.reset();
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
    history_.push_back({time_, observation.target, observation.bullets, observation.fires, observation.self,
                        observation.entities});
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
        nextGoal_ = nextShot_ = nextAttack_ = 0;
        attack_.reset();
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
    if (perceived_.target)
    {
        float toGoal = distance(observation.self.position, goal_);
        bool changed =
            perceived_.target->health != progressHealth_ || perceived_.target->shields != progressShields_ ||
            perceived_.target->barrier != progressBarrier_ || arena_.terrainRevision != progressTerrain_;
        if (distance(goal_, lastGoal_) > 12)
        {
            progressDistance_ = toGoal;
            lastGoal_ = goal_;
        }
        if (perceived_.target->health != progressHealth_ || perceived_.target->shields != progressShields_ ||
            perceived_.target->barrier != progressBarrier_)
            enemyProgressAt_ = time_;
        if (changed || toGoal < progressDistance_ - 8 || observation.self.weapon.reloading ||
            observation.self.weapon.overheated || observation.self.weapon.charging)
        {
            progressTime_ = time_;
            progressDistance_ = toGoal;
        }
        progressHealth_ = perceived_.target->health;
        progressShields_ = perceived_.target->shields;
        progressBarrier_ = perceived_.target->barrier;
        progressTerrain_ = arena_.terrainRevision;
        if (time_ - progressTime_ > 2 || time_ - enemyProgressAt_ > 6)
        {
            progressTime_ = enemyProgressAt_ = time_;
            repositionUntil_ = time_ + 1.f;
            cancelRoute(true);
            nextGoal_ = nextAttack_ = 0;
            attack_.reset();
            intent_ = BotIntent::Reposition;
        }
    }
    if (time_ >= nextGoal_ && observation.self.grounded && time_ >= dodgeUntil_)
    {
        auto next = chooseGoal(observation.self, perceived_.target);
        if (observation.objective && (observation.self.health > 25 || !perceived_.target))
        {
            Vec2 capture{observation.objective->x - observation.self.size.x / 2,
                         observation.objective->y - observation.self.size.y / 2};
            if (!perceived_.target ||
                distance(observation.self.weaponOrigin(), *observation.objective) > 50 ||
                distance(center(*perceived_.target), *observation.objective) > 72)
                next = capture;
        }
        if (distance(next, goal_) > 12)
            cancelRoute(false);
        goal_ = next;
        nextGoal_ = time_ + .1f;
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
        Player predicted =
            predictBotTarget(estimate, perceived_.self, std::max(0.f, time_ - perceived_.time), arena_);
        estimate.position = predicted.position;
        estimate.velocity = predicted.velocity;
        estimate.grounded = predicted.grounded;
        estimate.jetpackFuel = predicted.jetpackFuel;
        estimate.jetpackActive = predicted.jetpackActive;
        estimate.motionPhase = predicted.motionPhase;
        estimate.lean = predicted.lean;
        estimate.landing = predicted.landing;
        bool demolitionShot = false;
        if (time_ >= nextAttack_)
        {
            attack_ = botAim(observation.self, estimate, arena_, error_);
            if (!attack_)
            {
                Vec2 origin = observation.self.weaponOrigin(), target = center(estimate),
                     delta{target.x - origin.x, target.y - origin.y};
                float length = std::max(.01f, std::hypot(delta.x, delta.y)), nearest = 2;
                Vec2 contact{};
                for (const auto& wall : arena_.platforms)
                    if (auto hit = collision::segmentHit(origin, target, wall, 0); hit && *hit < nearest)
                    {
                        nearest = *hit;
                        contact = {origin.x + delta.x * nearest, origin.y + delta.y * nearest};
                    }
                // A projectile-sized opening may still be too small for a body.
                // Widen the nearest blocking cell when the firing line is clear.
                if (nearest > 1)
                {
                    float radius = std::max(1.f, observation.self.size.x / 2 + 1);
                    const TerrainCell* blocking = nullptr;
                    for (const auto& c : arena_.terrain)
                        if (c.health)
                            if (auto hit = collision::segmentHit(origin, target, c.bounds, radius);
                                hit && *hit < nearest)
                            {
                                nearest = *hit;
                                blocking = &c;
                            }
                    if (blocking)
                        contact = {blocking->bounds.position.x + blocking->bounds.size.x / 2,
                                   blocking->bounds.position.y + blocking->bounds.size.y / 2};
                }
                const auto* cell = arena_.terrainAt(contact, 2);
                float reach =
                    meleeWeapon(observation.self.weapon.kind)
                        ? attackStats(observation.self.weapon.kind, observation.self.abilities).reach
                        : abilityProfile(observation.self.weapon.kind, observation.self.abilities).maximum;
                bool useful = cell && nearest * length <= reach &&
                              (observation.self.weapon.kind != WeaponKind::Flamethrower ||
                               cell->material == Material::Wood);
                float safety =
                    (observation.self.weapon.kind == WeaponKind::GrenadeLauncher ? 180.f : explosionRadius) *
                    observation.self.abilities.blastRadius();
                if (nearest <= 1 && useful &&
                    (!explosiveWeapon(observation.self.weapon.kind) || length * nearest > safety + 30))
                {
                    attack_ = ballisticWeapon(observation.self.weapon.kind) ||
                                      observation.self.weapon.kind == WeaponKind::Flamethrower
                                  ? aimAtPoint(observation.self, contact)
                                  : std::optional<BotAim>{BotAim{
                                        {(contact.x - origin.x) / std::max(.01f, distance(contact, origin)),
                                         (contact.y - origin.y) / std::max(.01f, distance(contact, origin))},
                                        0}};
                    if (attack_)
                        attack_->demolition = true;
                }
            }
            nextAttack_ = time_ + .1f;
        }
        auto solution = attack_;
        demolitionShot = solution && solution->demolition;
        // Tracking continues even outside weapon reach; an old downward aim cannot latch.
        Vec2 tracking{center(estimate).x - observation.self.weaponOrigin().x,
                      center(estimate).y - observation.self.weaponOrigin().y};
        if (!solution && std::hypot(tracking.x, tracking.y) > .001f)
        {
            float current = std::atan2(aim_.y, aim_.x), desired = std::atan2(tracking.y, tracking.x);
            float change = std::remainder(desired - current, 2 * pi);
            current += std::clamp(change, -(hard_ ? 10.f : 6.f) * dt, (hard_ ? 10.f : 6.f) * dt);
            aim_ = {std::cos(current), std::sin(current)};
        }
        if (solution)
        {
            float current = std::atan2(aim_.y, aim_.x);
            const float desired = std::atan2(solution->direction.y, solution->direction.x);
            float change = std::remainder(desired - current, 2 * pi);
            current += std::clamp(change, -(hard_ ? 10.f : 6.f) * dt, (hard_ ? 10.f : 6.f) * dt);
            aim_ = {std::cos(current), std::sin(current)};
            bool valid =
                demolitionShot || !meleeWeapon(observation.self.weapon.kind) ||
                meleeContact(observation.self.weaponOrigin(), aim_,
                             attackStats(observation.self.weapon.kind, observation.self.abilities).reach,
                             predicted, arena_,
                             attackStats(observation.self.weapon.kind, observation.self.abilities).halfAngle)
                    .has_value();
            alignedFire = std::abs(change) < .06f && valid;
            if (alignedFire)
                intent_ = demolitionShot ? BotIntent::Demolish : BotIntent::Attack;
            if (observation.target && time_ >= nextShot_ && observation.self.weapon.cooldown <= 0 &&
                alignedFire)
            {
                input.shoot = true;
                nextShot_ =
                    time_ + std::max(tuning_.shotInterval * observation.self.weapon.shotInterval / .25f,
                                     observation.self.weapon.shotInterval) /
                                (observation.self.abilities.fireRate() *
                                 (observation.self.overdrive > 0 ? 1.25f : 1.f) *
                                 (1 + .15f * observation.self.adrenaline()));
            }
        }
    }
    if (observation.self.weapon.charging)
        input.shoot = alignedFire && observation.target.has_value();
    if (observation.self.weapon.automatic())
        input.shoot = alignedFire && observation.target.has_value();
    if (attack_ && attack_->demolition && time_ < repositionUntil_)
        input.shoot = alignedFire = false;
    if (observation.self.weapon.kind == WeaponKind::Minigun && perceived_.target &&
        distance(center(*perceived_.target), observation.self.weaponOrigin()) >
            abilityProfile(WeaponKind::Minigun, observation.self.abilities).maximum &&
        (!observation.self.grounded || (attack_ && !attack_->demolition && attack_->expectedDamage < 3)))
        input.shoot = alignedFire = false;
    input.triggerHeld = input.shoot;
    input.aimDirection = aim_;
    const auto& d = weaponDefinition(observation.self.weapon.kind);
    if (observation.self.weapon.reloading)
    {
        input.shoot = input.triggerHeld = false;
        intent_ = BotIntent::Recover;
    }
    if (d.capacity && observation.self.weapon.ammo > 0 &&
        observation.self.weapon.ammo <= std::max(1, observation.self.weapon.capacity() / 4) &&
        perceived_.target && !attack_ && !observation.self.weapon.reloading)
        input.reload = true;
    if (observation.self.weapon.kind == WeaponKind::Minigun && observation.self.weapon.heat > .9f)
        input.shoot = input.triggerHeld = false;
    input.moveDirection =
        std::clamp(input.moveDirection - observation.self.pushX *
                                             std::exp(-(observation.self.grounded ? 10.f : 3.f) * dt) /
                                             observation.self.movementSpeed(),
                   -1.f, 1.f);
    if (time_ < repositionUntil_ && perceived_.target && time_ >= dodgeUntil_ && path_.empty())
    {
        float side = center(*perceived_.target).x >= observation.self.weaponOrigin().x ? 1.f : -1.f;
        float gap = distance(center(*perceived_.target), observation.self.weaponOrigin());
        auto profile = abilityProfile(observation.self.weapon.kind, observation.self.abilities);
        input.moveDirection = gap > profile.minimum + 60 ? side : -side;
        intent_ = BotIntent::Reposition;
    }
    bool closeContact =
        perceived_.target && distance(center(*perceived_.target), observation.self.weaponOrigin()) < 110;
    if (mobile_ && perceived_.target && !alignedFire && !closeContact && time_ >= dodgeUntil_ &&
        time_ >= repositionUntil_)
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
        opponent = predictBotTarget(*perceived_.target, perceived_.self,
                                    std::max(0.f, time_ - perceived_.time), arena_);
        if (mobility_.control(observation.self, goal_, known, perceived_.fires, input, dt, &opponent))
        {
            aim_ = input.aimDirection;
            cancelRoute(false);
        }
    }
    if (time_ >= repositionUntil_ && intent_ != BotIntent::Demolish && !alignedFire)
        intent_ = hazardCost(observation.self.position) > 0 ? BotIntent::Escape
                  : observation.self.weapon.reloading || observation.self.weapon.overheated
                      ? BotIntent::Recover
                  : perceived_.target && distance(goal_, observation.self.position) > 12 &&
                          distance(goal_, perceived_.target->position) >
                              distance(observation.self.position, perceived_.target->position)
                      ? BotIntent::Retreat
                      : BotIntent::Approach;
    if (time_ >= nextShot_ && observation.self.weapon.cooldown <= 0 &&
        !meleeWeapon(observation.self.weapon.kind))
    {
        for (const auto& entity : perceived_.entities)
        {
            bool target = entity.kind == EntityKind::Screen ||
                          (entity.kind == EntityKind::Mine && entity.attack.shooterId != observation.self.id);
            if (!target)
                continue;
            Vec2 center{entity.position.x + entity.size.x / 2, entity.position.y + entity.size.y / 2};
            auto d = Vec2{center.x - observation.self.weaponOrigin().x,
                          center.y - observation.self.weaponOrigin().y};
            float length = std::hypot(d.x, d.y);
            if (length > 450 || length < .01f)
                continue;
            if (entity.kind == EntityKind::Screen &&
                !collision::segmentHit(observation.self.weaponOrigin(), goal_, entity.bounds(), 3))
                continue;
            if (terrainHit(observation.self.weaponOrigin(), center, arena_, 3) <= 1)
                continue;
            if (explosiveWeapon(observation.self.weapon.kind) ||
                observation.self.weapon.kind == WeaponKind::StickyLauncher)
                continue;
            input.aimDirection = {d.x / length, d.y / length};
            input.shoot = true;
            input.triggerHeld = observation.self.weapon.automatic();
            break;
        }
    }
    if (input.shoot &&
        (ballisticWeapon(observation.self.weapon.kind) || observation.self.weapon.kind == WeaponKind::Beam ||
         observation.self.weapon.kind == WeaponKind::Electric))
    {
        // Shots can hit a barrel before their intended target. Check the first
        // obstruction using delayed observations, and keep a safe HP reserve.
        const auto from = observation.self.weaponOrigin();
        const Vec2 to{from.x + input.aimDirection.x * 300, from.y + input.aimDirection.y * 300};
        const auto shot = observation.self.shotPrototype(input.aimDirection);
        float nearest = terrainHit(from, to, arena_, shot.collisionRadius);
        const WorldEntity* first = nullptr;
        for (const auto& e : perceived_.entities)
            if (e.health > 0 && !(e.attack.shooterId == observation.self.id && e.arm > 0 &&
                                  e.kind != EntityKind::Screen && e.kind != EntityKind::Barrel))
                if (auto hit = collision::segmentHit(from, to, e.bounds(), shot.collisionRadius);
                    hit && *hit < nearest)
                {
                    nearest = *hit;
                    first = &e;
                }
        if (first && first->kind == EntityKind::Barrel)
        {
            const Vec2 center{first->position.x + first->size.x / 2, first->position.y + first->size.y / 2};
            const auto blast = blastEffect(center, observation.self, arena_, 0, 150, 70);
            const int harm =
                healthDamage(blast.damage, shot.power, observation.self, WeaponKind::GrenadeLauncher);
            if (harm > 0 && observation.self.health - harm < 40)
            {
                input.shoot = input.triggerHeld = false;
                repositionUntil_ = std::max(repositionUntil_, time_ + .6f);
                intent_ = BotIntent::Reposition;
            }
        }
    }
    useActives(observation, input);
    input.jetpackHeld = input.jetpackHeld || input.jump;
    return input;
}

} // namespace cubeshot
