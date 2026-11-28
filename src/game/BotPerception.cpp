#include "game/AttackQueries.h"
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
            return {.18f, 1.75f, .25f, .55f};
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
            result.target = BotTarget{p.position,
                                      p.velocity,
                                      p.size,
                                      p.grounded,
                                      p.weapon.kind,
                                      p.aimDirection,
                                      p.health,
                                      p.weapon.charging,
                                      p.weapon.overheated,
                                      p.weapon.shots,
                                      p.weapon.reloading,
                                      p.pushX,
                                      p.abilities,
                                      p.shields,
                                      p.jetpackFuel,
                                      p.jetpackActive,
                                      p.id,
                                      p.surface,
                                      p.motionPhase,
                                      p.landing,
                                      p.lean,
                                      p.invulnerability,
                                      p.airJumps,
                                      p.secondChanceUsed,
                                      p.actives,
                                      p.barrier,
                                      p.bleeding,
                                      p.dashTime,
                                      p.dashDirection,
                                      p.barrierTime,
                                      p.slamTime,
                                      p.slamming};
    }
    if (result.target)
        for (const auto& p : world.players())
            if (p.id == opponent)
            {
                auto& t = *result.target;
                t.modifiers = p.modifiers;
                t.parry = p.parry;
                t.slow = p.slow;
                t.overdrive = p.overdrive;
                t.cleanse = p.cleanse;
                t.tempering = p.tempering;
                t.spawnProtection = p.spawnProtection;
                t.lifeRevision = p.lifeRevision;
            }
    for (auto entity : world.entities())
        if (entity.kind != EntityKind::Mine || world.canHarm(entity.attack.shooterId, self))
            result.entities.push_back(entity);
    for (const auto& b : world.bullets())
        if ((world.canHarm(b.shooterId, self) && b.shooterId != self) || explosiveWeapon(b.weapon) ||
            (b.weapon == WeaponKind::Disc && b.bounces > 0))
            result.bullets.push_back(b);
    for (auto fire : world.fires())
        if (world.canHarm(fire.owner, self))
            result.fires.push_back(fire);
    return result;
}

namespace botdetail
{
Player predictedTarget(const BotTarget& target)
{
    Player p;
    p.setAbilities(target.abilities, false);
    p.modifiers = target.modifiers;
    p.parry = target.parry;
    p.slow = target.slow;
    p.overdrive = target.overdrive;
    p.cleanse = target.cleanse;
    p.tempering = target.tempering;
    p.spawnProtection = target.spawnProtection;
    p.lifeRevision = target.lifeRevision;
    p.id = target.id;
    p.surface = target.surface;
    p.motionPhase = target.motionPhase;
    p.landing = target.landing;
    p.lean = target.lean;
    p.grounded = target.grounded;
    p.invulnerability = target.invulnerability;
    p.airJumps = target.airJumps;
    p.secondChanceUsed = target.secondChanceUsed;
    p.position = target.position;
    p.velocity = target.velocity;
    p.pushX = target.pushX;
    p.size = target.size;
    p.health = target.health;
    p.shields = target.shields;
    p.actives = target.actives;
    p.barrier = target.barrier;
    p.bleeding = target.bleeding;
    p.dashTime = target.dashTime;
    p.dashDirection = target.dashDirection;
    p.barrierTime = target.barrierTime;
    p.slamTime = target.slamTime;
    p.slamming = target.slamming;
    p.jetpackFuel = target.jetpackFuel;
    PlayerInput input;
    input.moveDirection = (target.velocity.x - target.pushX) / p.movementSpeed();
    input.aimDirection = target.aim;
    input.jetpackHeld = target.jetpackActive;
    p.applyInput(input);
    return p;
}

Player predictBotTarget(const BotTarget& target, const Player& observer, float seconds, const Arena& arena)
{
    Player other = predictedTarget(target), self = observer;
    self.id = observer.id ? observer.id : 1;
    other.id = target.id;
    self.clearInput();
    self.applyInput({std::clamp((observer.velocity.x - observer.pushX) / observer.movementSpeed(), -1.f, 1.f),
                     false, false, observer.aimDirection, false, false, observer.jetpackActive});
    std::vector<Player> bodies{self, other};
    seconds = std::clamp(seconds, 0.f, 2.f);
    for (float t = 0; t < seconds; t += step)
        simulateBodies(bodies, arena, std::min(step, seconds - t));
    for (const auto& body : bodies)
        if (body.id == other.id)
            return body;
    return other;
}

} // namespace botdetail

} // namespace cubeshot
