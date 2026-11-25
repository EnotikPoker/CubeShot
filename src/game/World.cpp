#include "game/World.h"
#include "game/AttackStats.h"
#include "game/BoomerangMotion.h"

#include "game/BodyPhysics.h"
#include "game/Collision.h"
#include "game/ProjectileMotion.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace cubeshot
{

World::World(Arena arena, std::size_t playerCount)
    : arena_(std::move(arena)), playerCount_(playerCount ? playerCount : arena_.spawnPoints.size())
{
    if (playerCount_ > arena_.spawnPoints.size())
        throw std::invalid_argument("Not enough arena spawn points");
    resetPlayers();
}

void World::resetPlayers(bool swapSpawns)
{
    testActiveRequests_ = {};
    arena_.restoreTerrain();
    debris_.clear();
    fires_.clear();
    players_.clear();
    events_.clear();
    bullets_.clear();
    for (std::size_t spawnIndex = 0; spawnIndex < playerCount_; ++spawnIndex)
    {
        const Vec2 spawn = arena_.spawnPoints[spawnIndex];
        Player player;
        player.id = static_cast<PlayerId>(players_.size()) + localPlayerId;
        player.position = spawn;
        player.grounded = collision::isSupported(player.bounds(), arena_.bounds, arena_.platforms);
        players_.push_back(player);
    }
    if (swapSpawns && players_.size() == 2)
    {
        std::swap(players_[0].position, players_[1].position);
    }
    if (players_.size() == 2)
    {
        const float direction = players_[0].position.x < players_[1].position.x ? 1.0f : -1.0f;
        players_[0].aimDirection = {direction, 0};
        players_[1].aimDirection = {-direction, 0};
    }
    resetEntities();
    pendingDamage_.assign(players_.size(), 0);
    pendingCauses_.assign(players_.size(), {});
    pendingContacts_.assign(players_.size(), {});
    pendingImpact_.assign(players_.size(), {});
}

void World::setHealth(PlayerId id, unsigned percent)
{
    for (auto& p : players_)
        if (p.id == id)
            p.health = std::max(1, static_cast<int>(p.healthLimit() * std::min(100u, percent) / 100));
}

void World::setAbilities(PlayerId id, const Abilities& abilities)
{
    for (auto& p : players_)
        if (p.id == id)
            p.setAbilities(abilities);
}

void World::requestAction(PlayerId id, bool jump, bool shoot, Vec2 aim, std::uint64_t sequence, bool reload,
                          unsigned activeMask)
{
    for (auto& player : players_)
    {
        if (player.id == id)
            player.requestAction(jump, shoot, aim, sequence, reload, activeMask);
    }
}

void World::setPlayerInput(PlayerId id, const PlayerInput& input)
{
    for (Player& player : players_)
    {
        if (player.id == id)
        {
            player.applyInput(input);
            return;
        }
    }
}

void World::clearPlayerInput(PlayerId id)
{
    for (Player& player : players_)
    {
        if (player.id == id)
        {
            player.clearInput();
            return;
        }
    }
}

void World::update(float deltaSeconds, bool damageLiving)
{
    if (!(deltaSeconds > 0.0f) || !std::isfinite(deltaSeconds))
    {
        return;
    }
    damageLiving_ = damageLiving;
    double remaining = deltaSeconds;
    // Match and prediction pass a float tick. A double 1/120 otherwise leaves
    // a tiny second tick that can cancel recoil against a ceiling after firing.
    constexpr double maxStep = static_cast<double>(1.0f / 120.0f);
    while (remaining > 0.0)
    {
        const float step = static_cast<float>(std::min(remaining, maxStep));
        remaining -= std::min(remaining, maxStep);
        std::fill(pendingDamage_.begin(), pendingDamage_.end(), 0);
        for (auto& causes : pendingCauses_)
            causes.clear();
        for (auto& contacts : pendingContacts_)
            contacts.clear();
        ++tick_;
        updateEntities(step);
        for (auto& p : players_)
        {
            auto requests = p.activeRequests;
            if (p.id <= testActiveRequests_.size())
            {
                requests |= testActiveRequests_[p.id - 1];
                testActiveRequests_[p.id - 1] = 0;
            }
            p.activeRequests = 0;
            for (unsigned slot = 0; slot < 2; ++slot)
                if (requests & (1u << slot))
                    castActive(p, slot);
        }
        simulateBodies(players_, arena_, step);
        for (auto& p : players_)
            if (p.slamming && p.grounded)
            {
                p.slamming = false;
                radialActive(p, ActiveKind::Slam);
            }
        updateBleeding(step);
        debris_.update(step, arena_, players_);
        updateFire(step);
        updateBullets(step);
        for (Player& player : players_)
        {
            if (auto bullet = player.takeShot())
            {
                const float shotSpeed = std::hypot(bullet->velocity.x, bullet->velocity.y);
                emit(EventKind::Shot, *bullet,
                     {bullet->position.x + bullet->velocity.x / shotSpeed * player.weapon.barrel(),
                      bullet->position.y + bullet->velocity.y / shotSpeed * player.weapon.barrel()});
                if (bullet->weapon == WeaponKind::Beam)
                {
                    beam(*bullet);
                    continue;
                }
                if (bullet->weapon == WeaponKind::Electric)
                {
                    electric(*bullet);
                    continue;
                }
                if (meleeWeapon(bullet->weapon))
                {
                    melee(*bullet);
                    continue;
                }
                const float speed = std::hypot(bullet->velocity.x, bullet->velocity.y);
                const Vec2 muzzle{bullet->position.x + bullet->velocity.x / speed * player.weapon.barrel(),
                                  bullet->position.y + bullet->velocity.y / speed * player.weapon.barrel()};
                for (auto projectile : scatterShot(*bullet))
                {
                    projectile.id = nextBulletId_++;
                    if (moveBullet(projectile, muzzle) && bullets_.size() < 256)
                        bullets_.push_back(projectile);
                }
            }
        }
        // Все попадания одного шага применяются вместе: взаимная смерть даёт ничью.
        for (std::size_t i = 0; i < players_.size(); ++i)
        {
            bool wasAlive = players_[i].alive();
            const auto velocity = players_[i].velocity;
            players_[i].takeDamage(pendingDamage_[i]);
            if (wasAlive && !players_[i].alive())
            {
                const auto& causes = pendingCauses_[i];
                Bullet marker;
                marker.shooterId = players_[i].id;
                if (!causes.empty())
                    marker = *std::max_element(
                        causes.begin(), causes.end(), [](const auto& a, const auto& b)
                        { return a.damage == b.damage ? a.id > b.id : a.damage < b.damage; });
                const float speed = std::hypot(marker.velocity.x, marker.velocity.y);
                const Vec2 deathImpact = speed > .001f
                                             ? Vec2{marker.velocity.x / speed, marker.velocity.y / speed}
                                             : pendingImpact_[i];
                const int volleyDamage = players_[i].lastDamageActor == marker.shooterId &&
                                                 players_[i].lastDamageVolley == marker.volley &&
                                                 players_[i].lastDamageWeapon == marker.weapon
                                             ? players_[i].lastVolleyDamage
                                             : marker.damage;
                ImpactContext cause;
                float total = 0;
                Vec2 sum{};
                unsigned seed = 0;
                for (const auto& contact : pendingContacts_[i])
                    if (contact.weapon == marker.weapon && contact.shooter == marker.shooterId &&
                        contact.volley == marker.volley)
                    {
                        cause = contact;
                        total += contact.damage;
                        sum.x += contact.local.x * contact.damage;
                        sum.y += contact.local.y * contact.damage;
                        seed ^= contact.seed;
                    }
                if (total > 0)
                {
                    cause.local = {sum.x / total, sum.y / total};
                    cause.seed = seed;
                    cause.damage = volleyDamage;
                    players_[i].lastImpact = cause;
                }
                removeOwnedEntities(players_[i].id);
                debris_.spawn(players_[i], velocity, deathImpact, marker.weapon, volleyDamage);
                emit(EventKind::Death, marker, players_[i].position, players_[i].id);
                events_.back().volleyDamage = volleyDamage;
            }
        }
        entities_.erase(
            std::remove_if(entities_.begin(), entities_.end(), [](const auto& e) { return e.health <= 0; }),
            entities_.end());
        syncScreens();
    }
}

bool World::moveBullet(Bullet& bullet, Vec2 destination)
{
    float nearest = 2.0f;
    Player* victim = nullptr;
    Rect hitBox{};
    bool hitSurface = false;
    auto candidates =
        arena_.solidsNear({{std::min(bullet.position.x, destination.x) - bullet.collisionRadius,
                            std::min(bullet.position.y, destination.y) - bullet.collisionRadius},
                           {std::abs(destination.x - bullet.position.x) + 2 * bullet.collisionRadius,
                            std::abs(destination.y - bullet.position.y) + 2 * bullet.collisionRadius}});
    for (const Rect& platform : candidates)
    {
        const auto hit =
            collision::segmentHit(bullet.position, destination, platform, bullet.collisionRadius);
        if (hit && *hit < nearest)
        {
            nearest = *hit;
            hitBox = platform;
            hitSurface = true;
        }
    }
    auto entityHit = traceEntities(bullet.position, destination, bullet.collisionRadius, bullet);
    if (entityHit && entityHit->first < nearest)
    {
        nearest = entityHit->first;
        hitSurface = false;
    }
    else
        entityHit.reset();
    for (Player& player : players_)
    {
        if (!player.alive() ||
            (player.id == bullet.shooterId && !(bullet.weapon == WeaponKind::Disc && bullet.bounces > 0)) ||
            (bullet.weapon == WeaponKind::FireBoomerang && (bullet.hitMask & (1u << player.id))))
        {
            continue;
        }
        const auto hit =
            hitPolygon({bullet.position.x - player.position.x, bullet.position.y - player.position.y},
                       {destination.x - player.position.x, destination.y - player.position.y},
                       player.bodyContour(), bullet.collisionRadius);
        // При равном расстоянии платформа закрывает игрока.
        if (hit && *hit < nearest)
        {
            nearest = *hit;
            victim = &player;
            entityHit.reset();
            hitSurface = false;
        }
    }
    auto remains = debris_.trace(bullet.position, destination, bullet.collisionRadius);
    if (remains && remains->fraction < nearest)
    {
        nearest = remains->fraction;
        victim = nullptr;
        entityHit.reset();
        hitSurface = false;
    }
    else
        remains.reset();
    if (nearest <= 1.0f)
    {
        Vec2 at{bullet.position.x + (destination.x - bullet.position.x) * nearest,
                bullet.position.y + (destination.y - bullet.position.y) * nearest};
        bullet.travelled =
            std::max(0.f, bullet.travelled - std::hypot(destination.x - at.x, destination.y - at.y));
        if (victim && victim->parry > 0 && bullet.active == ActiveKind::None &&
            ballisticWeapon(bullet.weapon))
        {
            float speed = std::max(.01f, std::hypot(bullet.velocity.x, bullet.velocity.y));
            if ((-bullet.velocity.x * victim->aimDirection.x - bullet.velocity.y * victim->aimDirection.y) /
                    speed >=
                .5f)
            {
                emit(EventKind::ShieldBlock, bullet, at, victim->id);
                if (bullet.reflections)
                    return false;
                ++bullet.reflections;
                bullet.shooterId = victim->id;
                bullet.ownerLife = victim->lifeRevision;
                bullet.velocity = {victim->aimDirection.x * speed, victim->aimDirection.y * speed};
                bullet.position = {at.x + victim->aimDirection.x * 5, at.y + victim->aimDirection.y * 5};
                return true;
            }
        }
        if (bullet.weapon == WeaponKind::StickyLauncher)
        {
            if (entities_.size() < maxWorldEntities)
            {
                WorldEntity e;
                e.id = nextEntityId_++;
                e.kind = EntityKind::Sticky;
                e.position = {at.x - 6, at.y - 6};
                e.size = {12, 12};
                e.health = 10;
                e.arm = .2f;
                e.life = 8;
                e.fuse = 1.25f;
                e.anchored = true;
                e.attack = bullet;
                e.attack.blastDamage = 55;
                e.attack.blastRadius = 135;
                e.attack.terrainDamage = 100;
                e.attack.terrainRadius = 80;
                if (victim)
                {
                    e.attached = victim->id;
                    e.attachedLife = victim->lifeRevision;
                    e.anchor = victim->bodyLocal({at.x - victim->position.x, at.y - victim->position.y});
                    damage(*victim, bullet, at, 5);
                }
                entities_.push_back(e);
            }
            return false;
        }
        if (entityHit)
        {
            if (explosiveWeapon(bullet.weapon))
                explode(bullet, at);
            else
                hitEntity(entityHit->second, bullet, at);
            return false;
        }
        if (explosiveWeapon(bullet.weapon) && (bullet.weapon != WeaponKind::GrenadeLauncher || victim))
        {
            explode(bullet, at, victim ? victim->id : 0);
            return false;
        }
        if (hitSurface && bullet.weapon == WeaponKind::GrenadeLauncher)
        {
            bounceProjectile(bullet, at, hitBox, .55f);
            return true;
        }
        auto impact = bullet;
        impact.damage = bullet.impactDamage();
        bool applied = true;
        if (remains)
        {
            const auto owner = remains->fragment ? debris_.pieces()[remains->index].owner
                                                 : debris_.corpses()[remains->index].body.owner;
            const bool broken = debris_.hit(*remains, impact, at);
            emit(broken ? EventKind::CorpseBreak : EventKind::CorpseHit, impact, at, owner);
        }
        else if (victim)
        {
            applied = damage(*victim, impact, at, impact.damage, incendiaryWeapon(bullet.weapon));
            if (bullet.active == ActiveKind::Harpoon && damageLiving_ && victim->spawnProtection <= 0)
            {
                for (const auto& owner : players_)
                    if (owner.id == bullet.shooterId)
                    {
                        Vec2 delta{owner.position.x - victim->position.x,
                                   owner.position.y - victim->position.y};
                        float n = std::max(.01f, std::hypot(delta.x, delta.y));
                        float force = 650 * (victim->abilities.level(AbilityKind::Stability) ? .5f : 1);
                        victim->applyImpulse({delta.x / n * force, delta.y / n * force});
                        emit(EventKind::ActiveCast, bullet, at, victim->id);
                        events_.back().local = owner.weaponOrigin();
                        events_.back().radius = 0;
                    }
            }
        }
        else
        {
            emit(EventKind::SurfaceImpact, impact, at);
            const auto* cell = arena_.terrainAt(at, bullet.collisionRadius + 1);
            unsigned group = cell ? cell->group : 0;
            Material material = cell ? cell->material : Material::Fixed;
            Vec2 materialContact{std::clamp(at.x, hitBox.position.x, hitBox.position.x + hitBox.size.x),
                                 std::clamp(at.y, hitBox.position.y, hitBox.position.y + hitBox.size.y)};
            arena_.damageTerrain(materialContact,
                                 bullet.collisionRadius + (material == Material::Metal ? 4 : 2),
                                 static_cast<int>(impact.damage * bullet.terrainPower),
                                 bullet.weapon == WeaponKind::Flamethrower);
            if (tunnelProjectile(bullet, arena_, at, group, material))
                return true;
            if (bullet.ricochets)
            {
                bounceProjectile(bullet, at, hitBox, 1);
                --bullet.ricochets;
                ++bullet.bounces;
                bullet.damage = std::max(1, static_cast<int>(std::lround(bullet.damage * .7f)));
                return true;
            }
        }
        if (incendiaryWeapon(bullet.weapon) && applied)
            igniteSurface(at, bullet.shooterId);
        if (bullet.weapon == WeaponKind::FireBoomerang)
        {
            if (victim)
                bullet.hitMask |= 1u << victim->id;
            else if (!bullet.returning)
            {
                bullet.returning = true;
                bullet.hitMask = 0;
            }
            else
                return false;
            const float speed = std::hypot(bullet.velocity.x, bullet.velocity.y);
            bullet.position = victim
                                  ? destination
                                  : Vec2{at.x - bullet.velocity.x / speed, at.y - bullet.velocity.y / speed};
            return true;
        }
        return false;
    }
    bullet.position = destination;
    const Rect& bounds = arena_.bounds;
    const bool inside = destination.x >= bounds.position.x + bullet.collisionRadius &&
                        destination.x <= bounds.position.x + bounds.size.x - bullet.collisionRadius &&
                        destination.y >= bounds.position.y + bullet.collisionRadius &&
                        destination.y <= bounds.position.y + bounds.size.y - bullet.collisionRadius;
    if (!inside && bullet.weapon == WeaponKind::FireBoomerang && !bullet.returning)
    {
        bullet.returning = true;
        bullet.hitMask = 0;
        bullet.position = {
            std::clamp(destination.x, bounds.position.x + 4, bounds.position.x + bounds.size.x - 4),
            std::clamp(destination.y, bounds.position.y + 4, bounds.position.y + bounds.size.y - 4)};
        return true;
    }
    if (!inside && explosiveWeapon(bullet.weapon))
        explode(bullet, bullet.position);
    return inside;
}

void World::updateBullets(float deltaSeconds)
{
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [&](Bullet& bullet)
                                  {
                                      if (bullet.weapon == WeaponKind::FireBoomerang)
                                      {
                                          const Player* owner = nullptr;
                                          for (const auto& p : players_)
                                              if (p.id == bullet.shooterId)
                                                  owner = &p;
                                          if (owner &&
                                              !steerBoomerang(bullet, owner->weaponOrigin(), owner->alive()))
                                              return true;
                                      }
                                      const Vec2 start = bullet.position;
                                      bullet.update(deltaSeconds);
                                      const Vec2 destination = bullet.position;
                                      bullet.position = start;
                                      if (bullet.lifetime <= 0)
                                      {
                                          if (explosiveWeapon(bullet.weapon))
                                              explode(bullet, start);
                                          return true;
                                      }
                                      return !moveBullet(bullet, destination);
                                  }),
                   bullets_.end());
}

void World::emit(EventKind kind, const Bullet& bullet, Vec2 at, PlayerId target)
{
    // A caller that does not consume presentation events must not grow memory indefinitely.
    if (events_.size() >= 256)
        events_.erase(events_.begin());
    float len = std::hypot(bullet.velocity.x, bullet.velocity.y);
    events_.push_back({nextEvent_++, tick_, bullet.shotSequence, 0, kind, bullet.shooterId, target, at,
                       len > 0 ? Vec2{bullet.velocity.x / len, bullet.velocity.y / len} : Vec2{1, 0},
                       bullet.damage, bullet.weapon, bullet.volley});
    events_.back().active = bullet.active;
    events_.back().ownerLife = bullet.ownerLife;
    if (target)
        for (const auto& player : players_)
            if (player.id == target)
            {
                events_.back().targetLife = player.lifeRevision;
                events_.back().local = player.bodyLocal({at.x - player.position.x, at.y - player.position.y});
                events_.back().surfaceRevision = player.surface.revision;
            }
}

std::vector<GameEvent> World::takeEvents()
{
    auto out = std::move(events_);
    events_.clear();
    return out;
}

const Arena& World::arena() const
{
    return arena_;
}

const std::vector<Player>& World::players() const
{
    return players_;
}

const std::vector<Bullet>& World::bullets() const
{
    return bullets_;
}

void World::equip(PlayerId id, WeaponKind kind)
{
    for (auto& p : players_)
        if (p.id == id)
        {
            p.clearInput();
            p.weapon.equip(kind);
        }
}

const Debris& World::debris() const
{
    return debris_;
}
} // namespace cubeshot
