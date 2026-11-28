#include "game/Prediction.h"
#include "game/BodyPhysics.h"
#include "game/BoomerangMotion.h"
#include "game/Collision.h"
#include "game/ProjectileMotion.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cubeshot
{
void Prediction::reset()
{
    others_.clear();
    events_.clear();
    shown_.clear();
    visualCooldown_ = 0;
    player_.reset();
    commands_.clear();
    actions_.clear();
    bullets_.clear();
    flight_.clear();
    sequence_ = actionAck_ = 0;
    round_ = 0;
    phase_ = MatchPhase::Lobby;
    offset_ = {};
    waitingForSnapshot_ = false;
}

void Prediction::simulate(const PredictedCommand& command, bool visuals)
{
    auto input = command.input;
    input.jump = input.shoot = input.reload = input.activeQ = input.activeE = false;
    player_->applyInput(input);
    for (const auto& a : actions_)
        if (a.input == command.sequence)
            player_->requestAction(a.jump, a.shoot, a.aim, a.sequence, a.reload, a.activeMask);
    const int authoritativeHealth = player_->health;
    auto requests = player_->activeRequests;
    player_->activeRequests = 0;
    for (unsigned slot = 0; slot < 2; ++slot)
        if (requests & (1u << slot))
            player_->activate(slot, arena_, others_);
    auto bodies = others_;
    bodies.push_back(*player_);
    simulateBodies(bodies, arena_, step);
    others_.clear();
    for (const auto& p : bodies)
    {
        if (p.id == player_->id)
            *player_ = p;
        else
            others_.push_back(p);
    }
    player_->health = authoritativeHealth;
    advanceRockets();
    // The same weapon function consumes ammo and adds recoil during prediction/replay.
    if (auto bullet = player_->takeShot())
    {
        if (bullet->weapon == WeaponKind::Rpg)
        {
            auto b = *bullet;
            float speed = std::hypot(b.velocity.x, b.velocity.y);
            Vec2 end{b.position.x + b.velocity.x / speed * player_->weapon.barrel(),
                     b.position.y + b.velocity.y / speed * player_->weapon.barrel()};
            float fraction = 2;
            for (const auto& wall : arena_.platforms)
                if (auto t = collision::segmentHit(b.position, end, wall, b.collisionRadius))
                    fraction = std::min(fraction, *t);
            if (fraction <= 1)
            {
                auto e = blastEffect({b.position.x + (end.x - b.position.x) * fraction,
                                      b.position.y + (end.y - b.position.y) * fraction},
                                     *player_, arena_, 0, explosionRadius * b.blastScale);
                if (phase_ == MatchPhase::Round && e.damage < player_->health)
                    player_->applyImpulse(e.impulse);
            }
            else
            {
                b.position = end;
                flight_.push_back(b);
            }
        }
        if (visuals)
            if (auto event = presentShot(*bullet))
                events_.push_back(*event);
    }
}

void Prediction::advanceRockets()
{
    flight_.erase(
        std::remove_if(
            flight_.begin(), flight_.end(),
            [&](Bullet& b)
            {
                Vec2 from = b.position;
                b.update(step);
                float fraction = 2;
                for (const auto& wall : arena_.platforms)
                    if (auto t = collision::segmentHit(from, b.position, wall, b.collisionRadius))
                        fraction = std::min(fraction, *t);
                for (const auto& other : others_)
                    if (canHarm(b.shooterId, other.id))
                        if (other.alive())
                            if (auto t = hitPolygon(
                                    {from.x - other.position.x, from.y - other.position.y},
                                    {b.position.x - other.position.x, b.position.y - other.position.y},
                                    other.bodyContour(), b.collisionRadius))
                                fraction = std::min(fraction, *t);
                bool outside = b.position.x < arena_.bounds.position.x ||
                               b.position.y < arena_.bounds.position.y ||
                               b.position.x > arena_.bounds.position.x + arena_.bounds.size.x ||
                               b.position.y > arena_.bounds.position.y + arena_.bounds.size.y;
                if (fraction <= 1 || outside || b.lifetime <= 0)
                {
                    Vec2 at{from.x + (b.position.x - from.x) * std::min(1.f, fraction),
                            from.y + (b.position.y - from.y) * std::min(1.f, fraction)};
                    auto effect = blastEffect(at, *player_, arena_, 0, explosionRadius * b.blastScale);
                    if (phase_ == MatchPhase::Round && effect.damage < player_->health)
                        player_->applyImpulse(effect.impulse);
                    return true;
                }
                return false;
            }),
        flight_.end());
}

void Prediction::reconcile(const WorldSnapshot& snapshot, PlayerId own)
{
    const auto found = std::find_if(snapshot.players.begin(), snapshot.players.end(),
                                    [own](const Player& p) { return p.id == own; });
    if (found == snapshot.players.end() || own < 1 || own > maxPlayers)
    {
        reset();
        return;
    }
    const bool teleport = player_ && player_->teleportRevision != found->teleportRevision;
    const bool boundary =
        (player_ && (player_->lifeRevision != found->lifeRevision || player_->alive() != found->alive())) ||
        arena_.map != snapshot.map || waitingForSnapshot_ || !player_ || round_ != snapshot.round ||
        phase_ != snapshot.phase || !found->alive();
    if (arena_.map != snapshot.map || !player_)
        arena_ = Arena::create(snapshot.map);
    teams_ = snapshot.rules.format == MatchFormat::Teams ? snapshot.teams : std::array<TeamId, maxPlayers>{};
    friendlyFire_ = snapshot.rules.friendlyFire;
    others_.clear();
    for (const auto& p : snapshot.players)
        if (p.id != own)
            others_.push_back(p);
    const auto old = player_ ? player_->position : found->position;
    const auto ack = snapshot.inputAck[own - 1];
    if (boundary)
    {
        visualCooldown_ = 0;
        sequence_ = ack;
        commands_.clear();
        actions_.clear();
        bullets_.clear();
        shown_.clear();
        events_.clear();
        offset_ = {};
    }
    arena_.applyTerrain(snapshot.terrain);
    arena_.modifiers = chaosModifiers(snapshot.mode.chaos);
    arena_.combatScreens.clear();
    for (auto e : snapshot.entities)
        if (e.kind == EntityKind::Screen)
            arena_.combatScreens.push_back(e.bounds());
    waitingForSnapshot_ = false;
    round_ = snapshot.round;
    phase_ = snapshot.phase;
    player_ = *found;
    sequence_ = std::max(sequence_, ack);
    actionAck_ = snapshot.actionAck[own - 1];
    while (!commands_.empty() && commands_.front().sequence <= ack)
        commands_.pop_front();
    while (!actions_.empty() && actions_.front().sequence <= actionAck_)
        actions_.pop_front();
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [&](const Bullet& b)
                                  {
                                      return b.volley <= found->weapon.shots ||
                                             (!weaponDefinition(b.weapon).automatic &&
                                              b.shotSequence <= actionAck_);
                                  }),
                   bullets_.end());
    flight_.clear();
    for (const auto& b : snapshot.bullets)
        if (b.shooterId == own && b.weapon == WeaponKind::Rpg)
            flight_.push_back(b);
    for (const auto& c : commands_)
        simulate(c);
    if (teleport)
        offset_ = {};
    else if (!boundary)
    {
        offset_.x += old.x - player_->position.x;
        offset_.y += old.y - player_->position.y;
        if (std::hypot(offset_.x, offset_.y) > 96)
            offset_ = {};
    }
}

void Prediction::advance(PlayerInput input)
{
    if (waitingForSnapshot_ || !player_ || (phase_ != MatchPhase::Round && phase_ != MatchPhase::RoundEnd) ||
        !player_->alive())
        return;
    // Two seconds maximum; recover from the next authoritative snapshot.
    if (commands_.size() >= 240)
    {
        commands_.clear();
        actions_.clear();
        bullets_.clear();
        waitingForSnapshot_ = true;
        return;
    }
    visualCooldown_ = std::max(0.f, visualCooldown_ - step);
    input.lifeRevision = player_->lifeRevision;
    PredictedCommand command{++sequence_, input};
    commands_.push_back(command);
    simulate(command, true);
    offset_.x *= .85f;
    offset_.y *= .85f;
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [&](Bullet& b)
                                  {
                                      if (b.weapon == WeaponKind::FireBoomerang &&
                                          !steerBoomerang(b, player_->weaponOrigin()))
                                          return true;
                                      return !advanceProjectileTerrain(b, arena_, step);
                                  }),
                   bullets_.end());
}

std::optional<GameEvent> Prediction::action(std::uint64_t sequence, bool jump, bool shoot, Vec2 aim,
                                            bool reload, unsigned activeMask)
{
    if (!player_ || (phase_ != MatchPhase::Round && phase_ != MatchPhase::RoundEnd) || !player_->alive() ||
        actions_.size() >= 240)
        return {};
    actions_.push_back({sequence, sequence_ + 1, jump, shoot, reload, aim, activeMask});
    for (unsigned slot = 0; slot < 2; ++slot)
        if (activeMask & (1u << slot))
        {
            auto visual = *player_;
            visual.aimDirection = aim;
            if (visual.activate(slot, arena_, others_))
            {
                GameEvent event;
                event.ownerLife = visual.lifeRevision;
                event.round = round_;
                event.actor = visual.id;
                event.kind = EventKind::ActiveCast;
                event.active = visual.actives[slot].kind;
                event.shotSequence = sequence;
                event.position = visual.weaponOrigin();
                event.local = player_->position;
                event.direction = visual.aimDirection;
                event.radius = event.active == ActiveKind::Wave                        ? 150.f
                               : event.active == ActiveKind::Slam && player_->grounded ? 110.f
                                                                                       : 0;
                events_.push_back(event);
            }
        }
    if (shoot && visualCooldown_ <= 0)
    {
        auto visual = *player_;
        visual.requestAction(false, true, aim, sequence);
        if (auto bullet = visual.takeShot())
        {
            visualCooldown_ = visual.weapon.cooldown;
            return presentShot(*bullet);
        }
    }
    return {};
}

std::optional<GameEvent> Prediction::presentShot(const Bullet& prototype)
{
    if (!shown_.insert(prototype.volley).second)
        return {};
    if (shown_.size() > 512)
        shown_.erase(shown_.begin());
    const float speed = std::hypot(prototype.velocity.x, prototype.velocity.y);
    const auto& definition = weaponDefinition(prototype.weapon);
    const Vec2 direction{prototype.velocity.x / speed, prototype.velocity.y / speed};
    float barrel = player_ ? player_->weapon.barrel() : definition.barrel;
    const Vec2 muzzle{prototype.position.x + direction.x * barrel,
                      prototype.position.y + direction.y * barrel};
    bool blocked = false;
    for (const auto& rect : arena_.platforms)
        if (collision::segmentHit(prototype.position, muzzle, rect, prototype.collisionRadius))
            blocked = true;
    if (prototype.weapon == WeaponKind::Electric || prototype.weapon == WeaponKind::Beam)
    {
        Vec2 end{prototype.position.x +
                     direction.x * (prototype.weapon == WeaponKind::Beam ? 560 : 600) * prototype.rangeScale,
                 prototype.position.y +
                     direction.y * (prototype.weapon == WeaponKind::Beam ? 560 : 600) * prototype.rangeScale};
        float fraction = 1;
        for (auto& r : arena_.platforms)
            if (auto hit = collision::segmentHit(prototype.position, end, r, 0))
                fraction = std::min(fraction, *hit);
        for (auto screen : arena_.combatScreens)
            if (auto hit = collision::segmentHit(prototype.position, end, screen, 0))
                fraction = std::min(fraction, *hit);
        for (auto& other : others_)
            if (canHarm(prototype.shooterId, other.id))
                if (other.alive())
                    if (auto hit = collision::segmentHit(prototype.position, end, other.bounds(), 0))
                        fraction = std::min(fraction, *hit);
        GameEvent arc{0,
                      0,
                      prototype.shotSequence,
                      round_,
                      EventKind::ElectricArc,
                      prototype.shooterId,
                      0,
                      {prototype.position.x + (end.x - prototype.position.x) * fraction,
                       prototype.position.y + (end.y - prototype.position.y) * fraction},
                      direction,
                      prototype.damage,
                      prototype.weapon,
                      prototype.volley};
        arc.ownerLife = prototype.ownerLife;
        arc.local = prototype.position;
        events_.push_back(arc);
    }
    if (!blocked && !meleeWeapon(prototype.weapon) && prototype.weapon != WeaponKind::Electric &&
        prototype.weapon != WeaponKind::Beam)
    {
        unsigned pellet = 0;
        for (auto bullet : scatterShot(prototype))
        {
            bullet.id = std::numeric_limits<BulletId>::max() - static_cast<BulletId>(prototype.volley) * 32 -
                        pellet++;
            bullet.position = muzzle;
            if (bullets_.size() < 256)
                bullets_.push_back(bullet);
        }
    }
    GameEvent shot{
        0,      0,         prototype.shotSequence, round_,           EventKind::Shot, prototype.shooterId, 0,
        muzzle, direction, prototype.damage,       prototype.weapon, prototype.volley};
    shot.ownerLife = prototype.ownerLife;
    return shot;
}

std::vector<GameEvent> Prediction::takeEvents()
{
    auto result = std::move(events_);
    events_.clear();
    return result;
}
} // namespace cubeshot
