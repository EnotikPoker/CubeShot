#include "game/World.h"

#include "game/Collision.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace cubeshot {

World::World(Arena arena) : arena_(std::move(arena))
{
    for (const Vec2 spawn : arena_.spawnPoints) {
        Player player;
        player.id = static_cast<PlayerId>(players_.size()) + localPlayerId;
        player.position = spawn;
        player.grounded = collision::isSupported(player.bounds(), arena_.bounds, arena_.platforms);
        players_.push_back(player);
    }
}

void World::setPlayerInput(PlayerId id, const PlayerInput& input)
{
    for (Player& player : players_) {
        if (player.id == id) {
            player.applyInput(input);
            return;
        }
    }
}

void World::update(float deltaSeconds)
{
    if (!(deltaSeconds > 0.0f) || !std::isfinite(deltaSeconds)) {
        return;
    }
    double remaining = deltaSeconds;
    constexpr double maxStep = 1.0 / 120.0;
    while (remaining > 0.0) {
        const float step = static_cast<float>(std::min(remaining, maxStep));
        remaining -= std::min(remaining, maxStep);
        for (Player& player : players_) {
            player.update(step, arena_);
        }
        updateBullets(step);
        for (Player& player : players_) {
            if (auto bullet = player.takeShot()) {
                if (moveBullet(*bullet, player.muzzlePosition())) {
                    bullets_.push_back(*bullet);
                }
            }
        }
    }
}

bool World::moveBullet(Bullet& bullet, Vec2 destination)
{
    float nearest = 2.0f;
    Player* victim = nullptr;
    for (const Rect& platform : arena_.platforms) {
        const auto hit = collision::segmentHit(bullet.position, destination, platform, Bullet::radius);
        if (hit && *hit < nearest) {
            nearest = *hit;
        }
    }
    for (Player& player : players_) {
        if (!player.alive() || player.id == bullet.shooterId) {
            continue;
        }
        const auto hit = collision::segmentHit(bullet.position, destination, player.bounds(), Bullet::radius);
        // При равном расстоянии платформа закрывает игрока.
        if (hit && *hit < nearest) {
            nearest = *hit;
            victim = &player;
        }
    }
    if (nearest <= 1.0f) {
        if (victim) {
            victim->takeDamage(bullet.damage);
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
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [&](Bullet& bullet) {
        const Vec2 start = bullet.position;
        bullet.update(deltaSeconds);
        const Vec2 destination = bullet.position;
        bullet.position = start;
        return bullet.lifetime <= 0.0f || !moveBullet(bullet, destination);
    }), bullets_.end());
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

} // namespace cubeshot
