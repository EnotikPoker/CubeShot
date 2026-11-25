#include "game/AttackStats.h"
#include "game/World.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
void World::setActives(PlayerId id, const ActiveLoadout& loadout)
{
    for (auto& p : players_)
        if (p.id == id)
            p.setActives(loadout);
}

void World::castActive(Player& p, unsigned slot)
{
    if (slot >= 2)
        return;
    auto kind = p.actives[slot].kind;
    Vec2 old = p.position;
    if ((kind == ActiveKind::Mine || kind == ActiveKind::Screen) && entities_.size() >= maxWorldEntities &&
        std::none_of(entities_.begin(), entities_.end(),
                     [&](auto e)
                     {
                         return e.attack.shooterId == p.id &&
                                e.kind == (kind == ActiveKind::Mine ? EntityKind::Mine : EntityKind::Screen);
                     }))
        return;
    if (!p.activate(slot, arena_, players_))
        return;
    Bullet marker;
    marker.shooterId = p.id;
    marker.ownerLife = p.lifeRevision;
    marker.active = kind;
    marker.position = p.weaponOrigin();
    marker.velocity = p.aimDirection;
    marker.shotSequence = p.activeSequence;
    marker.volley = 0x80000000u | (++p.activeSerial);
    marker.power = p.abilities.power();
    marker.terrainPower = p.abilities.demolition() * p.modifiers.terrain;
    marker.blastScale = p.abilities.blastRadius();
    marker.ignites = p.abilities.level(AbilityKind::Incendiary) > 0;
    if (kind == ActiveKind::Slam)
    {
        p.slamVolley = marker.volley;
        p.slamSequence = marker.shotSequence;
    }
    emit(EventKind::ActiveCast, marker, marker.position);
    events_.back().local = old;
    events_.back().radius = 0;
    if (kind == ActiveKind::Wave || (kind == ActiveKind::Slam && p.grounded))
    {
        p.slamming = false;
        radialActive(p, kind);
    }
    if (kind == ActiveKind::Mine || kind == ActiveKind::Screen)
    {
        for (auto& e : entities_)
            if (e.kind == (kind == ActiveKind::Mine ? EntityKind::Mine : EntityKind::Screen) &&
                e.attack.shooterId == p.id)
                e.health = 0;
        entities_.erase(
            std::remove_if(entities_.begin(), entities_.end(), [](auto& e) { return e.health <= 0; }),
            entities_.end());
        if (entities_.size() < maxWorldEntities)
        {
            WorldEntity e;
            e.id = nextEntityId_++;
            e.attack = marker;
            if (kind == ActiveKind::Mine)
            {
                e.kind = EntityKind::Mine;
                e.position = {p.position.x + p.size.x / 2 - 8, p.position.y + p.size.y - 10};
                e.size = {16, 10};
                e.arm = .6f;
                e.life = 12;
                e.health = 15;
                e.attack.blastDamage = 45;
                e.attack.blastRadius = 130;
                e.attack.terrainDamage = 60;
                e.attack.terrainRadius = 64;
            }
            else
            {
                e.kind = EntityKind::Screen;
                e.position = {p.position.x + p.size.x / 2 + p.aimDirection.x * 120 - 5,
                              p.position.y + p.size.y - 112};
                e.size = {10, 112};
                e.life = 3;
                e.health = 90;
            }
            entities_.push_back(e);
            syncScreens();
        }
    }
    if (kind == ActiveKind::Harpoon || kind == ActiveKind::Bomb || kind == ActiveKind::IceGrenade)
    {
        marker.id = nextBulletId_++;
        marker.damage = kind == ActiveKind::IceGrenade ? 15 : kind == ActiveKind::Bomb ? 60 : 10;
        float speed = (kind == ActiveKind::Bomb || kind == ActiveKind::IceGrenade) ? 520.f : 1100.f;
        marker.velocity = {p.aimDirection.x * speed * p.modifiers.projectiles,
                           p.aimDirection.y * speed * p.modifiers.projectiles};
        marker.lifetime = kind == ActiveKind::IceGrenade ? .8f
                          : kind == ActiveKind::Bomb     ? 1.1f
                                                         : 600.f / 1100;
        marker.acceleration = (kind == ActiveKind::Bomb || kind == ActiveKind::IceGrenade) ? 900.f : 0;
        marker.weapon = (kind == ActiveKind::Bomb || kind == ActiveKind::IceGrenade)
                            ? WeaponKind::GrenadeLauncher
                            : WeaponKind::Pistol;
        marker.collisionRadius = (kind == ActiveKind::Bomb || kind == ActiveKind::IceGrenade) ? 5.f : 3.f;
        if (kind == ActiveKind::IceGrenade)
        {
            marker.blastDamage = 15;
            marker.blastRadius = 130;
            marker.terrainDamage = 0;
            marker.terrainRadius = 0;
        }
        if (bullets_.size() < 256)
            bullets_.push_back(marker);
    }
}

void World::radialActive(Player& p, ActiveKind kind)
{
    Bullet b;
    b.shooterId = p.id;
    b.ownerLife = p.lifeRevision;
    b.area = true;
    b.active = kind;
    b.shotSequence = kind == ActiveKind::Slam ? p.slamSequence : p.activeSequence;
    b.volley = kind == ActiveKind::Slam ? p.slamVolley : (0x80000000u | p.activeSerial);
    b.power = p.abilities.power();
    b.terrainPower = p.abilities.demolition() * p.modifiers.terrain;
    b.ignites = p.abilities.level(AbilityKind::Incendiary) > 0;
    float radius = (kind == ActiveKind::Slam ? 110.f : 150.f) * p.abilities.blastRadius();
    b.damage = kind == ActiveKind::Slam ? 40 : 15;
    Vec2 at = kind == ActiveKind::Slam ? Vec2{p.position.x + p.size.x / 2, p.position.y + p.size.y - 1}
                                       : p.weaponOrigin();
    emit(EventKind::ActiveCast, b, at);
    events_.back().radius = radius;
    for (auto& victim : players_)
        if (victim.id != p.id && victim.alive())
        {
            auto effect = blastEffect(at, victim, arena_, 0, radius, b.damage);
            if (effect.damage > 0)
            {
                b.velocity = effect.impulse;
                damage(victim, b, effect.contact, b.damage);
                float force = (kind == ActiveKind::Slam ? 550.f : 700.f) *
                              (victim.abilities.level(AbilityKind::Stability) ? .5f : 1);
                float n = std::max(1.f, std::hypot(effect.impulse.x, effect.impulse.y));
                if (damageLiving_ && victim.spawnProtection <= 0)
                {
                    victim.applyImpulse({effect.impulse.x / n * force, effect.impulse.y / n * force});
                    victim.lastHostileAgo = 0;
                }
            }
        }
    if (kind == ActiveKind::Slam)
        arena_.damageTerrain(at, 64 * p.abilities.blastRadius(), int(60 * b.terrainPower), false, true);
}

void World::updateBleeding(float dt)
{
    for (auto& p : players_)
        if (p.alive() && p.bleeding > 0)
        {
            p.bleeding = std::max(0.f, p.bleeding - dt);
            p.bleedTick += dt;
            while (p.bleedTick >= 1.f - .0001f)
            {
                p.bleedTick = std::max(0.f, p.bleedTick - 1);
                Bullet b;
                b.shooterId = p.bleedOwner;
                b.weapon = WeaponKind::Knife;
                b.power = p.bleedPower;
                damage(p, b, p.weaponOrigin(), 4, false, DamageContext{true, false, true});
            }
        }
}
} // namespace cubeshot
