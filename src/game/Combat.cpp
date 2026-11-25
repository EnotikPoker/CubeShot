#include "game/AttackQueries.h"
#include "game/AttackStats.h"
#include "game/Collision.h"
#include "game/World.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cubeshot
{
namespace
{
Vec2 nearestPoint(Vec2 at, Rect box)
{
    return {std::clamp(at.x, box.position.x, box.position.x + box.size.x),
            std::clamp(at.y, box.position.y, box.position.y + box.size.y)};
}

bool blocked(Vec2 from, Vec2 to, const Arena& arena)
{
    for (auto screen : arena.combatScreens)
        if (auto t = collision::segmentHit(from, to, screen, 0); t && *t < .98f)
            return true;
    for (const auto& platform : arena.platforms)
        if (const auto hit = collision::segmentHit(from, to, platform, 0); hit && *hit < .98f)
            return true;
    return false;
}
} // namespace

int explosionDamage(float distance)
{
    return std::max(0, static_cast<int>(std::lround(120 * (1 - distance / explosionRadius))));
}

BlastEffect blastEffect(Vec2 at, const Player& p, const Arena& arena, PlayerId direct, float radius,
                        int maximumDamage)
{
    BlastEffect effect;
    effect.contact = nearestPoint(at, p.bounds());
    const float d = std::hypot(effect.contact.x - at.x, effect.contact.y - at.y);
    if (d < radius && !blocked(at, effect.contact, arena))
    {
        effect.damage = std::max(0, static_cast<int>(std::lround(maximumDamage * (1 - d / radius))));
        Vec2 dir{p.position.x + p.size.x / 2 - at.x, p.position.y + p.size.y / 2 - at.y};
        float n = std::hypot(dir.x, dir.y);
        if (n < .001f)
        {
            dir = {0, -1};
            n = 1;
        }
        float force = explosionImpulse * (1 - d / radius);
        effect.impulse = {dir.x / n * force, dir.y / n * force};
    }
    if (p.id == direct)
        effect.damage = std::max(std::min(100, maximumDamage), effect.damage);
    return effect;
}

bool World::damage(Player& player, const Bullet& bullet, Vec2 at, int amount, bool ignite,
                   DamageContext flags)
{
    const bool periodic = flags.periodic, reflected = flags.reflected;
    if (!damageLiving_ || amount <= 0 || !player.alive() || player.invulnerability > 0 ||
        player.spawnProtection > 0)
        return false;
    if (bullet.shooterId && bullet.shooterId != player.id && !flags.environmental)
        player.lastHostileAgo = 0;
    const auto shields = player.shields;
    if (!periodic && !reflected && !flags.environmental && player.absorb(bullet))
    {
        if (player.shields != shields)
            emit(EventKind::ShieldBlock, bullet, at, player.id);
        return false;
    }
    const bool hostile = bullet.shooterId && bullet.shooterId != player.id;
    float factor = reflected ? 1.f : bullet.power;
    if (!periodic && !reflected && !flags.environmental)
    {
        if (meleeWeapon(player.weapon.kind) && player.abilities.level(AbilityKind::Stance) && !bullet.area)
        {
            float n = std::max(.01f, std::hypot(bullet.velocity.x, bullet.velocity.y));
            if ((-bullet.velocity.x * player.aimDirection.x - bullet.velocity.y * player.aimDirection.y) /
                    n >=
                .5f)
                factor *= .8f;
        }
        if (player.parry > 0 && meleeWeapon(bullet.weapon) && !bullet.area)
        {
            float n = std::max(.01f, std::hypot(bullet.velocity.x, bullet.velocity.y));
            if ((-bullet.velocity.x * player.aimDirection.x - bullet.velocity.y * player.aimDirection.y) /
                    n >=
                .5f)
            {
                emit(EventKind::ShieldBlock, bullet, at, player.id);
                return false;
            }
        }
        if (hostile && player.health * 10 < player.healthLimit() * 3)
            for (auto& attacker : players_)
                if (attacker.id == bullet.shooterId && attacker.abilities.level(AbilityKind::Executioner) &&
                    attacker.lifeRevision == bullet.ownerLife)
                    factor *= 1.15f;
    }
    amount = healthDamage(amount, factor, player, bullet.weapon, periodic && !flags.bleeding);
    if (player.barrier > 0)
    {
        float absorbed = std::min(float(amount), player.barrier);
        player.barrier -= absorbed;
        amount -= int(absorbed);
        if (absorbed > 0)
        {
            auto shield = bullet;
            shield.active = ActiveKind::Barrier;
            emit(EventKind::ShieldBlock, shield, at, player.id);
        }
        if (amount <= 0)
            return false;
    }
    // Segment tracing returns the projectile centre. With a large projectile and a
    // small player that centre can be nine canonical pixels outside the skin.
    // Damage marks and blood must attach to the real posed contour, not that centre.
    auto outline = player.bodyContour();
    Vec2 contact{at.x - player.position.x, at.y - player.position.y};
    if (!containsPoint(outline, contact))
    {
        Vec2 nearest = contact;
        float best = 1e30f;
        for (std::size_t i = 0; i < outline.size(); ++i)
        {
            auto a = outline[i], b = outline[(i + 1) % outline.size()];
            float dx = b.x - a.x, dy = b.y - a.y;
            float t = std::clamp(((contact.x - a.x) * dx + (contact.y - a.y) * dy) /
                                     std::max(.00001f, dx * dx + dy * dy),
                                 0.f, 1.f);
            Vec2 point{a.x + t * dx, a.y + t * dy};
            float distance = std::hypot(contact.x - point.x, contact.y - point.y);
            if (distance < best)
            {
                best = distance;
                nearest = point;
            }
        }
        at = {player.position.x + nearest.x, player.position.y + nearest.y};
    }
    auto event = bullet;
    event.damage = amount;
    emit(EventKind::Hit, event, at, player.id);
    auto hitEventId = events_.back().id;
    const auto index = static_cast<std::size_t>(&player - players_.data());
    const int actual = std::min(amount, std::max(0, player.health - pendingDamage_[index]));
    pendingDamage_[index] += amount;
    player.damageQuiet = 0;
    if (hostile && !flags.environmental)
        player.lastHostileAgo = 0;
    if (hostile && !periodic && !reflected && !bullet.secondary)
    {
        if (player.abilities.level(AbilityKind::Tempering) && player.temperCooldown <= 0)
        {
            player.tempering = 1.5f;
            player.temperCooldown = 5;
        }
        for (auto& attacker : players_)
            if (attacker.id == bullet.shooterId && attacker.lifeRevision == bullet.ownerLife)
            {
                auto key = (std::uint64_t(bullet.ownerLife) << 32) ^ bullet.volley ^
                           (std::uint64_t(player.id) << 60);
                if (attacker.abilities.level(AbilityKind::Impulse) && attacker.impulseKey != key)
                {
                    attacker.impulseKey = key;
                    float n = std::max(.01f, std::hypot(bullet.velocity.x, bullet.velocity.y));
                    player.applyImpulse(
                        {bullet.velocity.x / n * 80 * attacker.abilities.level(AbilityKind::Impulse),
                         bullet.velocity.y / n * 80 * attacker.abilities.level(AbilityKind::Impulse)});
                }
            }
    }
    if (!periodic && !reflected && !bullet.secondary && bullet.shooterId != player.id && actual > 0)
    {
        for (auto& attacker : players_)
            if (attacker.id == bullet.shooterId && attacker.alive() &&
                attacker.lifeRevision == bullet.ownerLife)
            {
                if (bullet.active == ActiveKind::None && meleeWeapon(bullet.weapon) &&
                    attacker.abilities.level(AbilityKind::Bleeding) && player.cleanse <= 0)
                {
                    if (player.bleeding <= 0)
                        player.bleedTick = 0;
                    player.bleeding = 3;
                    player.bleedPower = bullet.power;
                    player.bleedOwner = attacker.id;
                }
                if (attacker.abilities.level(AbilityKind::Vampirism))
                    attacker.heal(actual * .15f);
                if (player.abilities.level(AbilityKind::Thorns))
                {
                    auto reflectedBullet = bullet;
                    reflectedBullet.shooterId = player.id;
                    reflectedBullet.ignites = false;
                    reflectedBullet.power = 1;
                    reflectedBullet.velocity = {-bullet.velocity.x, -bullet.velocity.y};
                    damage(attacker, reflectedBullet, attacker.weaponOrigin(),
                           std::max(1, static_cast<int>(std::lround(actual * .15f))), false,
                           DamageContext{false, true});
                }
            }
    }
    const float speed = std::hypot(bullet.velocity.x, bullet.velocity.y);
    pendingImpact_[index] =
        speed > .001f ? Vec2{bullet.velocity.x / speed, bullet.velocity.y / speed} : Vec2{1, 0};
    if (player.cleanse <= 0 && !bullet.secondary && !reflected &&
        (ignite || (!periodic && bullet.ignites && bullet.shooterId != player.id)))
    {
        player.burning = 2.5f;
        player.burnOwner = bullet.shooterId;
    }
    auto& causes = pendingCauses_[index];
    auto source = std::find_if(causes.begin(), causes.end(),
                               [&](const auto& b) {
                                   return b.shooterId == bullet.shooterId && b.volley == bullet.volley &&
                                          b.weapon == bullet.weapon;
                               });
    if (source == causes.end())
    {
        auto contribution = bullet;
        contribution.damage = amount;
        causes.push_back(contribution);
    }
    else
        source->damage += amount;
    if (player.lastDamageActor != bullet.shooterId || player.lastDamageVolley != bullet.volley ||
        player.lastDamageWeapon != bullet.weapon)
    {
        player.lastDamageActor = bullet.shooterId;
        player.lastDamageVolley = bullet.volley;
        player.lastDamageWeapon = bullet.weapon;
        player.lastVolleyDamage = 0;
    }
    player.lastVolleyDamage += amount;
    const ImpactContext context{player.bodyLocal({at.x - player.position.x, at.y - player.position.y}),
                                pendingImpact_[index],
                                bullet.weapon,
                                amount,
                                bullet.shooterId,
                                bullet.volley,
                                static_cast<unsigned>(visualRandom_())};
    player.lastImpact = context;
    pendingContacts_[index].push_back(context);
    const auto material = carveImpact(player.surface, context);
    if (!material.empty())
        debris_.chip(player, material, context.direction, context.seed,
                     pendingDamage_[index] >= player.health);
    // Marks and particles use body-local coordinates, never the later interpolated world position.
    for (auto& hit : events_)
        if (hit.id == hitEventId)
            hit.surfaceRevision = player.surface.revision;
    return true;
}

void World::explode(const Bullet& bullet, Vec2 at, PlayerId direct)
{
    const auto explosion = explosionStats(bullet);
    const float radius = explosion.radius;
    emit(EventKind::Explosion, bullet, at);
    events_.back().radius = radius;
    debris_.blast(at, radius);
    for (auto& player : players_)
    {
        if (!player.alive())
            continue;
        auto effect = blastEffect(at, player, arena_, direct, radius, explosion.damage);
        if (bullet.shooterId != player.id && player.abilities.level(AbilityKind::Stability))
        {
            effect.impulse.x *= .5f;
            effect.impulse.y *= .5f;
        }
        if (effect.damage > 0)
        {
            auto impact = bullet;
            impact.area = true;
            impact.velocity = effect.impulse;
            bool dealt = damage(player, impact, effect.contact, effect.damage);
            if (bullet.active == ActiveKind::IceGrenade && player.cleanse <= 0 && dealt)
                player.slow = 2;
            if (damageLiving_ && player.spawnProtection <= 0)
            {
                player.applyImpulse(effect.impulse);
                if (bullet.shooterId && bullet.shooterId != player.id)
                    player.lastHostileAgo = 0;
            }
        }
    }
    auto targets = entities_;
    for (const auto& entity : targets)
        if (entity.health > 0)
        {
            auto center = Vec2{entity.position.x + entity.size.x / 2, entity.position.y + entity.size.y / 2};
            float d = std::hypot(center.x - at.x, center.y - at.y);
            if (d < radius)
            {
                auto hit = bullet;
                hit.damage = std::max(1, int(explosion.damage * (1 - d / radius)));
                hitEntity(entity.id, hit, center);
            }
        }
    for (std::size_t i = 0; i < debris_.corpses().size(); ++i)
    {
        const auto corpse = debris_.corpses()[i];
        const float distance = std::hypot(corpse.body.position.x - at.x, corpse.body.position.y - at.y);
        if (corpse.body.outline.empty() || distance >= radius)
            continue;
        auto impact = bullet;
        impact.damage = explosionDamage(distance);
        impact.velocity = {corpse.body.position.x - at.x, corpse.body.position.y - at.y - 20};
        const bool broken = debris_.hit({0, false, i}, impact, corpse.body.position);
        emit(broken ? EventKind::CorpseBreak : EventKind::CorpseHit, impact, corpse.body.position,
             corpse.body.owner);
    }
    const bool broken =
        arena_.damageTerrain(at, explosion.terrainRadius,
                             static_cast<int>(explosion.terrainDamage * bullet.terrainPower), false, true);
    if (broken)
        emit(EventKind::SurfaceImpact, bullet, at);
}

void World::igniteSurface(Vec2 at, PlayerId owner)
{
    // Flames attach to a nearby top surface, never hover in empty air.
    float closest = 75;
    std::optional<Vec2> surface;
    for (const auto& platform : arena_.platforms)
    {
        if (at.x < platform.position.x - 2 || at.x > platform.position.x + platform.size.x + 2)
            continue;
        const float d = platform.position.y - at.y;
        if (d >= -3 && d < closest)
        {
            closest = d;
            surface = Vec2{at.x, platform.position.y - 1};
        }
    }
    if (!surface)
        return;
    for (auto& fire : fires_)
        if (fire.owner == owner &&
            std::hypot(fire.position.x - surface->x, fire.position.y - surface->y) < 28)
        {
            fire.life = 3;
            return;
        }
    if (fires_.size() < maxFires)
        fires_.push_back({nextFireId_++, owner, *surface, 3, 12});
}

void World::updateFire(float dt)
{
    for (auto& fire : fires_)
    {
        fire.life -= dt;
        if (!collision::isSupported({{fire.position.x - 1, fire.position.y - 1}, {2, 2}}, arena_.bounds,
                                    arena_.platforms))
        {
            fire.life = 0;
            continue;
        }
        fire.radius = std::min(34.f, fire.radius + dt * 12);
        if (static_cast<int>((fire.life + dt) * 4) != static_cast<int>(fire.life * 4))
            arena_.damageTerrain(fire.position, fire.radius, 3, true);
        for (auto& player : players_)
        {
            const auto point = nearestPoint(fire.position, player.bounds());
            if (player.alive() && player.cleanse <= 0 &&
                std::hypot(point.x - fire.position.x, point.y - fire.position.y) < fire.radius &&
                damageLiving_)
            {
                player.burning = 2.5f;
                player.burnOwner = fire.owner;
            }
        }
    }
    fires_.erase(std::remove_if(fires_.begin(), fires_.end(), [](const auto& f) { return f.life <= 0; }),
                 fires_.end());
    for (auto& player : players_)
    {
        if (!player.alive())
        {
            player.burning = player.burnTick = 0;
            continue;
        }
        if (player.burning <= 0)
        {
            player.burnTick = 0;
            continue;
        }
        player.burning = std::max(0.f, player.burning - dt);
        player.burnTick += dt;
        if (player.burnTick >= .25f)
        {
            player.burnTick -= .25f;
            Bullet flame;
            flame.shooterId = player.burnOwner;
            flame.weapon = WeaponKind::Flamethrower;
            flame.velocity = {0, -1};
            damage(player, flame, player.weaponOrigin(), 3, false, DamageContext{true, false});
        }
    }
}

void World::melee(const Bullet& attack)
{
    Abilities traits;
    for (auto& p : players_)
        if (p.id == attack.shooterId)
            traits = p.abilities;
    auto stats = attackStats(attack.weapon, traits);
    const float reach = stats.reach;
    bool hitHealth = false;
    PlayerId hitTarget = 0;
    const float speed = std::hypot(attack.velocity.x, attack.velocity.y);
    const Vec2 aim{attack.velocity.x / speed, attack.velocity.y / speed};
    for (auto& player : players_)
    {
        if (!player.alive() || player.id == attack.shooterId)
            continue;
        if (auto contact = meleeContact(attack.position, aim, reach, player, arena_, stats.halfAngle))
            if (damage(player, attack, contact->point, attack.damage))
            {
                hitHealth = true;
                hitTarget = player.id;
                if (attack.weapon == WeaponKind::Hammer && player.spawnProtection <= 0)
                {
                    player.applyImpulse({aim.x * 450, aim.y * 450});
                    player.lastHostileAgo = 0;
                }
            }
    }
    for (auto& owner : players_)
        if (owner.id == attack.shooterId && !attack.secondary && hitHealth)
        {
            if (owner.abilities.level(AbilityKind::Echo) && ++owner.meleeSuccesses % 3 == 0)
            {
                auto echo = attack;
                echo.secondary = true;
                echo.damage = std::max(1, int(std::lround(attack.damage * .35f)));
                echoes_.push_back({echo, .12f});
            }
        }
    for (auto& owner : players_)
        if (owner.id == attack.shooterId && !attack.secondary && owner.abilities.level(AbilityKind::Combo))
        {
            if (hitHealth)
            {
                owner.comboHits = owner.comboTarget == hitTarget ? std::min(3u, owner.comboHits + 1) : 1;
                owner.comboTarget = hitTarget;
                owner.comboTime = 2;
            }
            else
            {
                owner.comboHits = 0;
                owner.comboTarget = 0;
                owner.comboTime = 0;
            }
        }
    const Vec2 end{attack.position.x + aim.x * reach, attack.position.y + aim.y * reach};
    float nearestTerrain = 2;
    Vec2 at{};
    for (auto& wall : arena_.platforms)
        if (auto hit = collision::segmentHit(attack.position, end, wall, 0); hit && *hit < nearestTerrain)
        {
            nearestTerrain = *hit;
            at = {attack.position.x + (end.x - attack.position.x) * *hit,
                  attack.position.y + (end.y - attack.position.y) * *hit};
        }
    if (auto object = traceEntities(attack.position, end, 0, attack);
        object && object->first < nearestTerrain)
        hitEntity(object->second, attack, end);
    if (nearestTerrain <= 1)
    {
        arena_.damageTerrain(at, 14,
                             static_cast<int>((attack.weapon == WeaponKind::Hammer ? 60 : attack.damage) *
                                              attack.terrainPower));
        emit(EventKind::SurfaceImpact, attack, at);
    }
    if (!blocked(attack.position, end, arena_))
        if (const auto hit = debris_.trace(attack.position, end))
        {
            const auto owner =
                hit->fragment ? debris_.pieces()[hit->index].owner : debris_.corpses()[hit->index].body.owner;
            const bool broken = debris_.hit(*hit, attack, end);
            emit(broken ? EventKind::CorpseBreak : EventKind::CorpseHit, attack, end, owner);
        }
}
} // namespace cubeshot
