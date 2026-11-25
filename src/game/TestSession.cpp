#include "game/TestSession.h"
#include "game/Collision.h"
#include "game/World.h"
#include <algorithm>

namespace cubeshot
{
bool World::editTest(const TestEdit& edit, const TestConfiguration& config)
{
    // Sandbox commands are never applicable to a live two-player Match.
    if (players_.size() != 4 || edit.actor >= players_.size())
        return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!validActiveLoadout(config.actives[i]) || !config.abilities[i].valid() ||
            !validWeapon(config.weapons[i]) || !config.healthPercent[i] || config.healthPercent[i] > 100)
            return false;
    if (edit.operation == TestOperation::AllBuilds)
    {
        World candidate = *this;
        for (unsigned i = 0; i < 4; ++i)
            if (!candidate.editTest({TestOperation::Build, i}, config))
                return false;
        *this = std::move(candidate);
        return true;
    }
    auto& p = players_[edit.actor];
    auto clearHazards = [&]
    {
        testActiveRequests_ = {};
        bullets_.clear();
        fires_.clear();
        entities_.clear();
        echoes_.clear();
        syncScreens();
        debris_.clear();
        events_.clear();
        for (auto& actor : players_)
        {
            actor.burning = actor.burnTick = actor.bleeding = actor.bleedTick = actor.barrier =
                actor.barrierTime = actor.dashTime = 0;
            actor.slow = actor.parry = actor.overdrive = actor.cleanse = actor.tempering = actor.resonance =
                0;
            actor.slamming = false;
        }
    };
    auto safe = [&](const Player& candidate)
    {
        auto b = candidate.bounds();
        if (b.position.x < arena_.bounds.position.x || b.position.y < arena_.bounds.position.y ||
            b.position.x + b.size.x > arena_.bounds.position.x + arena_.bounds.size.x ||
            b.position.y + b.size.y > arena_.bounds.position.y + arena_.bounds.size.y)
            return false;
        for (const auto& solid : arena_.platforms)
            if (collision::overlaps(b, solid))
                return false;
        for (const auto& other : players_)
            if (other.id != p.id && other.alive() && collision::overlaps(b, other.bounds()))
                return false;
        return true;
    };
    switch (edit.operation)
    {
        case TestOperation::Build:
        {
            auto next = p;
            next.setAbilities(config.abilities[edit.actor], false);
            if (p.alive() && !safe(next))
                return false;
            if (next.weapon.kind != config.weapons[edit.actor])
                next.weapon.equip(config.weapons[edit.actor]);
            for (unsigned slot = 0; slot < 2; ++slot)
                if (next.actives[slot].kind != config.actives[edit.actor][slot])
                {
                    auto old = next.actives[slot].kind;
                    testActiveRequests_[edit.actor] &= ~(1u << slot);
                    if (old == ActiveKind::Barrier)
                        next.barrier = next.barrierTime = 0;
                    if (old == ActiveKind::Dash)
                        next.dashTime = 0;
                    if (old == ActiveKind::Mine || old == ActiveKind::Screen)
                        removeOwnedEntities(next.id);
                    if (old == ActiveKind::Parry)
                        next.parry = 0;
                    if (old == ActiveKind::Overdrive)
                        next.overdrive = 0;
                    if (old == ActiveKind::Cleanse)
                        next.cleanse = 0;
                    if (old == ActiveKind::Slam)
                        next.slamming = false;
                    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [&](auto& b)
                                                  { return b.shooterId == next.id && b.active == old; }),
                                   bullets_.end());
                    next.actives[slot] = {config.actives[edit.actor][slot], 0};
                }
            p = std::move(next);
            break;
        }
        case TestOperation::Health:
            if (p.alive())
                setHealth(p.id, config.healthPercent[edit.actor]);
            break;
        case TestOperation::Heal:
            if (p.alive())
                p.health = p.healthLimit();
            break;
        case TestOperation::Respawn:
        {
            Player fresh;
            fresh.id = p.id;
            fresh.lifeRevision = p.lifeRevision + 1;
            fresh.position = arena_.spawnPoints[edit.actor];
            fresh.setAbilities(config.abilities[edit.actor]);
            fresh.weapon.equip(config.weapons[edit.actor]);
            fresh.setActives(config.actives[edit.actor]);
            fresh.weapon.shots = p.weapon.shots;
            fresh.health =
                std::max(1, static_cast<int>(fresh.healthLimit() * config.healthPercent[edit.actor] / 100));
            if (!safe(fresh))
                return false;
            fresh.grounded = collision::isSupported(fresh.bounds(), arena_.bounds, arena_.platforms);
            p = std::move(fresh);
            testActiveRequests_[edit.actor] = 0;
            debris_.eraseOwner(p.id);
            bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                          [&](auto& b) { return b.shooterId == p.id; }),
                           bullets_.end());
            break;
        }
        case TestOperation::EmptyShields:
            p.shields = 0;
            break;
        case TestOperation::RefillShields:
            p.refillShields();
            break;
        case TestOperation::EmptyFuel:
            p.jetpackFuel = 0;
            break;
        case TestOperation::RefillFuel:
            p.jetpackFuel = 2;
            break;
        case TestOperation::EmptyAmmo:
            p.weapon.cancelReload();
            p.weapon.ammo = 0;
            break;
        case TestOperation::RefillAmmo:
            p.weapon.cancelReload();
            p.weapon.ammo = p.weapon.capacity();
            break;
        case TestOperation::Reload:
            p.weapon.startReload();
            break;
        case TestOperation::FinishReload:
            if (p.weapon.reloading)
            {
                p.weapon.ammo = p.weapon.capacity();
                p.weapon.cancelReload();
                p.freshMagazine = true;
            }
            break;
        case TestOperation::Overheat:
            if (p.weapon.kind == WeaponKind::Minigun)
            {
                p.weapon.heat = 1;
                p.weapon.overheated = true;
            }
            break;
        case TestOperation::Cool:
            p.weapon.heat = 0;
            p.weapon.overheated = false;
            break;
        case TestOperation::Extinguish:
            p.burning = p.burnTick = 0;
            break;
        case TestOperation::RestoreMap:
            clearHazards();
            arena_.restoreTerrain();
            resetEntities();
            for (unsigned i = 0; i < players_.size(); ++i)
            {
                auto& actor = players_[i];
                actor.position = arena_.spawnPoints[i];
                actor.position.x += (48 - actor.size.x) / 2;
                actor.position.y += 48 - actor.size.y;
                actor.velocity = {};
                actor.pushX = 0;
                actor.clearInput();
                actor.grounded = collision::isSupported(actor.bounds(), arena_.bounds, arena_.platforms);
            }
            break;
        case TestOperation::ReadyActives:
            for (auto& slot : p.actives)
                slot.cooldown = 0;
            break;
        case TestOperation::UseQ:
            testActiveRequests_[edit.actor] |= 1;
            break;
        case TestOperation::UseE:
            testActiveRequests_[edit.actor] |= 2;
            break;
        case TestOperation::Clear:
            clearHazards();
            break;
        default:
            return false;
    }
    return true;
}
} // namespace cubeshot
