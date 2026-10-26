#include "game/World.h"

#include "game/BodyPhysics.h"
#include "game/Collision.h"

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
    debris_.clear();
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
    pendingDamage_.assign(players_.size(), 0);
    pendingImpact_.assign(players_.size(), {});
}

void World::requestAction(PlayerId id, bool jump, bool shoot, Vec2 aim, std::uint64_t sequence)
{
    for (auto& player : players_)
    {
        if (player.id == id)
            player.requestAction(jump, shoot, aim, sequence);
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
    constexpr double maxStep = 1.0 / 120.0;
    while (remaining > 0.0)
    {
        const float step = static_cast<float>(std::min(remaining, maxStep));
        remaining -= std::min(remaining, maxStep);
        std::fill(pendingDamage_.begin(), pendingDamage_.end(), 0);
        ++tick_;
        simulateBodies(players_, arena_, step);
        debris_.update(step, arena_, players_);
        updateBullets(step);
        for (Player& player : players_)
        {
            if (auto bullet = player.takeShot())
            {
                emit(EventKind::Shot, *bullet, player.muzzlePosition());
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
                debris_.spawn(players_[i], velocity, pendingImpact_[i]);
                Bullet marker;
                marker.shooterId = players_[i].id;
                marker.velocity = pendingImpact_[i];
                marker.damage = pendingDamage_[i];
                emit(EventKind::Death, marker, players_[i].position, players_[i].id);
            }
        }
    }
}

bool World::moveBullet(Bullet& bullet, Vec2 destination)
{
    float nearest = 2.0f;
    Player* victim = nullptr;
    for (const Rect& platform : arena_.platforms)
    {
        const auto hit = collision::segmentHit(bullet.position, destination, platform, Bullet::radius);
        if (hit && *hit < nearest)
        {
            nearest = *hit;
        }
    }
    for (Player& player : players_)
    {
        if (!player.alive() || player.id == bullet.shooterId)
        {
            continue;
        }
        const auto hit = collision::segmentHit(bullet.position, destination, player.bounds(), Bullet::radius);
        // При равном расстоянии платформа закрывает игрока.
        if (hit && *hit < nearest)
        {
            nearest = *hit;
            victim = &player;
        }
    }
    auto remains = debris_.trace(bullet.position, destination);
    if (remains && remains->fraction < nearest)
    {
        nearest = remains->fraction;
        victim = nullptr;
    }
    else
        remains.reset();
    if (nearest <= 1.0f)
    {
        Vec2 at{bullet.position.x + (destination.x - bullet.position.x) * nearest,
                bullet.position.y + (destination.y - bullet.position.y) * nearest};
        if (remains)
        {
            const auto owner = remains->fragment ? debris_.pieces()[remains->index].owner
                                                 : debris_.corpses()[remains->index].body.owner;
            const bool broken = debris_.hit(*remains, bullet, at);
            emit(broken ? EventKind::CorpseBreak : EventKind::CorpseHit, bullet, at, owner);
        }
        else
        {
            emit(victim && damageLiving_ ? EventKind::Hit : EventKind::SurfaceImpact, bullet, at,
                 victim && damageLiving_ ? victim->id : 0);
            if (victim && damageLiving_)
            {
                const auto index = static_cast<std::size_t>(victim - players_.data());
                pendingDamage_[index] += bullet.damage;
                const float speed = std::hypot(bullet.velocity.x, bullet.velocity.y);
                pendingImpact_[index] = {bullet.velocity.x / speed, bullet.velocity.y / speed};
            }
        }
        return false;
    }
    bullet.position = destination;
    const Rect& bounds = arena_.bounds;
    return destination.x >= bounds.position.x + Bullet::radius &&
           destination.x <= bounds.position.x + bounds.size.x - Bullet::radius &&
           destination.y >= bounds.position.y + Bullet::radius &&
           destination.y <= bounds.position.y + bounds.size.y - Bullet::radius;
}

void World::updateBullets(float deltaSeconds)
{
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [&](Bullet& bullet)
                                  {
                                      const Vec2 start = bullet.position;
                                      bullet.update(deltaSeconds);
                                      const Vec2 destination = bullet.position;
                                      bullet.position = start;
                                      return bullet.lifetime <= 0.0f || !moveBullet(bullet, destination);
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
