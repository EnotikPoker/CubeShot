#include "game/AttackStats.h"
#include "game/Collision.h"
#include "game/World.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
void World::setModifiers(SimulationModifiers m)
{
    arena_.modifiers = m;
    for (auto& p : players_)
    {
        p.modifiers = m;
        p.weapon.reloadFactor = m.reload / (p.overdrive > 0 ? 1.25f : 1.f);
    }
}

void World::syncScreens()
{
    arena_.combatScreens.clear();
    for (const auto& e : entities_)
        if (e.health > 0 && e.kind == EntityKind::Screen)
            arena_.combatScreens.push_back(e.bounds());
}

void World::resetEntities()
{
    arena_.combatScreens.clear();
    entities_.clear();
    echoes_.clear();
    hazardTick_ = 0;
    for (auto at : arena_.barrelSpawns)
    {
        WorldEntity e;
        e.id = nextEntityId_++;
        e.kind = EntityKind::Barrel;
        e.position = at;
        e.size = {24, 28};
        e.life = -1;
        entities_.push_back(e);
    }
    syncScreens();
}

void World::removeOwnedEntities(PlayerId id)
{
    for (auto& e : entities_)
        if (e.attack.shooterId == id && (e.kind == EntityKind::Mine || e.kind == EntityKind::Screen))
            e.health = 0;
    syncScreens();
}

void World::advanceDeadCooldowns(float dt)
{
    for (auto& p : players_)
        if (!p.alive())
        {
            for (auto& a : p.actives)
                a.cooldown = std::max(0.f, a.cooldown - dt);
            p.lastHostileAgo = std::min(999.f, p.lastHostileAgo + dt);
        }
}

void World::respawnPlayer(PlayerId id, Vec2 at, WeaponKind weapon, const Abilities& traits,
                          const ActiveLoadout& active, const ActiveSlots& previous, unsigned life)
{
    for (auto& p : players_)
        if (p.id == id)
        {
            Player next;
            next.id = id;
            next.lifeRevision = life;
            next.setAbilities(traits);
            next.position = at;
            next.weapon.equip(weapon);
            next.weapon.shots = p.weapon.shots;
            next.activeSerial = p.activeSerial;
            next.modifiers = p.modifiers;
            next.setActives(active);
            for (unsigned i = 0; i < 2; ++i)
                if (active[i] == previous[i].kind)
                    next.actives[i].cooldown = previous[i].cooldown;
            next.spawnProtection = 1.5f;
            next.grounded = collision::isSupported(next.bounds(), arena_.bounds, arena_.platforms);
            next.aimDirection = {at.x < arena_.bounds.size.x / 2 ? 1.f : -1.f, 0};
            p = next;
        }
}

std::optional<std::pair<float, std::uint64_t>> World::traceEntities(Vec2 from, Vec2 to, float radius,
                                                                    const Bullet& b) const
{
    std::optional<std::pair<float, std::uint64_t>> best;
    for (const auto& e : entities_)
        if (e.health > 0)
        {
            if (e.attack.shooterId == b.shooterId && e.kind != EntityKind::Screen &&
                e.kind != EntityKind::Barrel && e.arm > 0)
                continue;
            if (auto t = collision::segmentHit(from, to, e.bounds(), radius);
                t && (!best || *t < best->first))
                best = std::make_pair(*t, e.id);
        }
    return best;
}

void World::hitEntity(std::uint64_t id, const Bullet& b, Vec2 at)
{
    for (auto& e : entities_)
        if (e.id == id && e.health > 0)
        {
            e.health -= std::max(1, int(std::lround(b.impactDamage() * b.power)));
            if (e.health > 0)
                return;
            auto dead = e;
            syncScreens();
            if (dead.kind == EntityKind::Screen)
                return;
            auto blast = dead.attack;
            blast.position = {dead.position.x + dead.size.x / 2, dead.position.y + dead.size.y / 2};
            if (dead.kind == EntityKind::Barrel)
            {
                blast = b;
                blast.weapon = WeaponKind::GrenadeLauncher;
                blast.active = ActiveKind::None;
                blast.damage = 70;
                blast.blastDamage = 70;
                blast.blastRadius = 150;
                blast.terrainDamage = 120;
                blast.terrainRadius = 80;
                blast.id = dead.id;
                blast.volley = 0x40000000u | unsigned(dead.id);
            }
            blast.area = true;
            // The entity is already dead before recursive explosions; each can trigger once.
            explode(blast, blast.position);
            return;
        }
}

void World::updateEntities(float dt)
{
    for (std::size_t index = 0; index < entities_.size(); ++index)
    {
        auto& e = entities_[index];
        if (e.health <= 0)
            continue;
        if (e.life > 0)
        {
            e.life = std::max(0.f, e.life - dt);
            if (e.life == 0)
            {
                e.health = 0;
                continue;
            }
        }
        e.arm = std::max(0.f, e.arm - dt);
        if (e.kind == EntityKind::Sticky)
        {
            if (e.attached)
            {
                bool found = false;
                for (const auto& p : players_)
                    if (p.id == e.attached && p.lifeRevision == e.attachedLife && p.alive())
                    {
                        auto local = p.bodyPoint(e.anchor);
                        e.position = {p.position.x + local.x - e.size.x / 2,
                                      p.position.y + local.y - e.size.y / 2};
                        found = true;
                    }
                if (!found)
                {
                    e.attached = 0;
                    e.anchored = false;
                }
            }
            if (!e.anchored && !e.attached)
            {
                e.velocity.y += 650 * dt;
                auto moved =
                    collision::moveVertically(e.bounds(), e.velocity.y * dt, arena_.bounds, arena_.platforms);
                e.position.y = moved.y;
                if (moved.blocked)
                {
                    e.velocity = {};
                    e.anchored = true;
                }
            }
            e.fuse = std::max(0.f, e.fuse - dt);
        }
        bool trigger = e.kind == EntityKind::Sticky && e.fuse <= 0;
        if (e.kind == EntityKind::Mine && e.arm <= 0)
            for (auto& p : players_)
                if (p.alive() && p.id != e.attack.shooterId &&
                    std::hypot(p.weaponOrigin().x - e.position.x, p.weaponOrigin().y - e.position.y) < 100)
                    trigger = true;
        if (trigger)
        {
            auto id = e.id;
            Bullet destroy;
            destroy.damage = 100;
            hitEntity(id, destroy, e.position);
        }
    }
    entities_.erase(
        std::remove_if(entities_.begin(), entities_.end(), [](const auto& e) { return e.health <= 0; }),
        entities_.end());
    syncScreens();
    hazardTick_ += dt;
    while (hazardTick_ >= .25f)
    {
        hazardTick_ -= .25f;
        for (auto& p : players_)
            if (p.alive())
                for (const auto& hazard : arena_.hazards)
                {
                    Rect area = hazard.bounds;
                    area.position.y -= 3;
                    area.size.y += 6;
                    if (collision::overlaps(p.bounds(), area))
                    {
                        Bullet b;
                        b.weapon = hazard.heat ? WeaponKind::Flamethrower : WeaponKind::Knife;
                        damage(p, b, p.weaponOrigin(), 5, false, DamageContext{false, false, false, true});
                    }
                }
    }
    for (auto& e : echoes_)
        e.delay -= dt;
    auto echoes = std::move(echoes_);
    echoes_.clear();
    for (auto e : echoes)
        if (e.delay > 0)
            echoes_.push_back(e);
        else
            for (auto& p : players_)
                if (p.id == e.attack.shooterId && p.alive() && p.lifeRevision == e.attack.ownerLife)
                    melee(e.attack);
}

void World::beam(const Bullet& shot)
{
    Vec2 from = shot.position;
    float n = std::max(.001f, std::hypot(shot.velocity.x, shot.velocity.y));
    Vec2 end{from.x + shot.velocity.x / n * 560 * shot.rangeScale,
             from.y + shot.velocity.y / n * 560 * shot.rangeScale};
    float nearest = 1;
    Player* victim = nullptr;
    for (auto r : arena_.platforms)
        if (auto t = collision::segmentHit(from, end, r, 0))
            nearest = std::min(nearest, *t);
    auto object = traceEntities(from, end, 0, shot);
    if (object && object->first < nearest)
        nearest = object->first;
    else
        object.reset();
    for (auto& p : players_)
        if (p.id != shot.shooterId && p.alive())
            if (auto t = hitPolygon({from.x - p.position.x, from.y - p.position.y},
                                    {end.x - p.position.x, end.y - p.position.y}, p.bodyContour(), 0);
                t && *t < nearest)
            {
                nearest = *t;
                victim = &p;
                object.reset();
            }
    Vec2 at{from.x + (end.x - from.x) * nearest, from.y + (end.y - from.y) * nearest};
    if (victim)
        damage(*victim, shot, at, shot.damage);
    else if (object)
        hitEntity(object->second, shot, at);
    else
        arena_.damageTerrain(at, 5, int(shot.damage * shot.terrainPower));
    emit(EventKind::ElectricArc, shot, at);
}
} // namespace cubeshot
