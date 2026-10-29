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
    for (const auto& platform : arena.platforms)
        if (const auto hit = collision::segmentHit(from, to, platform, 0); hit && *hit < .98f)
            return true;
    return false;
}
} // namespace

void World::damage(Player& player, const Bullet& bullet, Vec2 at, int amount, bool ignite)
{
    if (!damageLiving_ || amount <= 0 || !player.alive())
        return;
    auto event = bullet;
    event.damage = amount;
    emit(EventKind::Hit, event, at, player.id);
    const auto index = static_cast<std::size_t>(&player - players_.data());
    pendingDamage_[index] += amount;
    const float speed = std::hypot(bullet.velocity.x, bullet.velocity.y);
    pendingImpact_[index] =
        speed > .001f ? Vec2{bullet.velocity.x / speed, bullet.velocity.y / speed} : Vec2{1, 0};
    if (ignite)
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
    if (incendiaryWeapon(bullet.weapon))
        player.surface.charred = true;
    else
    {
        const auto wanted =
            std::min(6u, static_cast<unsigned>(Player::maxHealth -
                                               std::max(0, player.health - pendingDamage_[index])) /
                             15);
        while (player.surface.count < wanted)
            if (!addEdgeChip(player.surface,
                             player.bodyLocal({at.x - player.position.x, at.y - player.position.y}),
                             bullet.volley + bullet.shooterId * 17 + player.surface.count))
                break;
    }
    // Marks and particles use body-local coordinates, never the later interpolated world position.
    events_.back().surfaceRevision = player.surface.revision;
}

void World::explode(const Bullet& bullet, Vec2 at)
{
    emit(EventKind::Explosion, bullet, at);
    debris_.blast(at, explosionRadius);
    for (auto& player : players_)
    {
        const auto point = nearestPoint(at, player.bounds());
        const float distance = std::hypot(point.x - at.x, point.y - at.y);
        if (distance < explosionRadius && !blocked(at, point, arena_))
        {
            auto impact = bullet;
            impact.velocity = {player.weaponOrigin().x - at.x, player.weaponOrigin().y - at.y};
            damage(player, impact, point,
                   std::max(1, static_cast<int>(100 * (1 - distance / explosionRadius))));
        }
    }
    for (std::size_t i = 0; i < debris_.corpses().size(); ++i)
    {
        const auto corpse = debris_.corpses()[i];
        const float distance = std::hypot(corpse.body.position.x - at.x, corpse.body.position.y - at.y);
        if (corpse.body.outline.empty() || distance >= explosionRadius)
            continue;
        auto impact = bullet;
        impact.damage = std::max(1, static_cast<int>(100 * (1 - distance / explosionRadius)));
        impact.velocity = {corpse.body.position.x - at.x, corpse.body.position.y - at.y - 20};
        const bool broken = debris_.hit({0, false, i}, impact, corpse.body.position);
        emit(broken ? EventKind::CorpseBreak : EventKind::CorpseHit, impact, corpse.body.position,
             corpse.body.owner);
    }
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
        fire.radius = std::min(34.f, fire.radius + dt * 12);
        for (auto& player : players_)
        {
            const auto point = nearestPoint(fire.position, player.bounds());
            if (player.alive() &&
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
            damage(player, flame, player.weaponOrigin(), 3);
        }
    }
}

void World::melee(const Bullet& attack)
{
    const float reach = weaponDefinition(attack.weapon).barrel;
    const float speed = std::hypot(attack.velocity.x, attack.velocity.y);
    const Vec2 aim{attack.velocity.x / speed, attack.velocity.y / speed};
    for (auto& player : players_)
    {
        if (!player.alive() || player.id == attack.shooterId)
            continue;
        const auto at = nearestPoint(attack.position, player.bounds());
        const Vec2 delta{at.x - attack.position.x, at.y - attack.position.y};
        const float distance = std::hypot(delta.x, delta.y);
        if (distance <= reach &&
            (distance < .001f || (delta.x * aim.x + delta.y * aim.y) / distance >= .7f) &&
            !blocked(attack.position, at, arena_))
            damage(player, attack, at, attack.damage);
    }
    const Vec2 end{attack.position.x + aim.x * reach, attack.position.y + aim.y * reach};
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
