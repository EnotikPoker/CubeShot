#include "game/Prediction.h"
#include "game/BodyPhysics.h"
#include "game/Collision.h"
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
    input.jump = input.shoot = input.reload = false;
    player_->applyInput(input);
    for (const auto& a : actions_)
        if (a.input == command.sequence)
            player_->requestAction(a.jump, a.shoot, a.aim, a.sequence, a.reload);
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
                if (auto t = collision::segmentHit(b.position, end, wall, Bullet::radius))
                    fraction = std::min(fraction, *t);
            if (fraction <= 1)
            {
                auto e = blastEffect({b.position.x + (end.x - b.position.x) * fraction,
                                      b.position.y + (end.y - b.position.y) * fraction},
                                     *player_, arena_);
                if (e.damage < player_->health)
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
        std::remove_if(flight_.begin(), flight_.end(),
                       [&](Bullet& b)
                       {
                           Vec2 from = b.position;
                           b.update(step);
                           float fraction = 2;
                           for (const auto& wall : arena_.platforms)
                               if (auto t = collision::segmentHit(from, b.position, wall, Bullet::radius))
                                   fraction = std::min(fraction, *t);
                           for (const auto& other : others_)
                               if (other.alive())
                                   if (auto t = hitPolygon(
                                           {from.x - other.position.x, from.y - other.position.y},
                                           {b.position.x - other.position.x, b.position.y - other.position.y},
                                           other.bodyContour(), Bullet::radius))
                                       fraction = std::min(fraction, *t);
                           bool outside = b.position.x < arena_.bounds.position.x ||
                                          b.position.y < arena_.bounds.position.y ||
                                          b.position.x > arena_.bounds.position.x + arena_.bounds.size.x ||
                                          b.position.y > arena_.bounds.position.y + arena_.bounds.size.y;
                           if (fraction <= 1 || outside || b.lifetime <= 0)
                           {
                               Vec2 at{from.x + (b.position.x - from.x) * std::min(1.f, fraction),
                                       from.y + (b.position.y - from.y) * std::min(1.f, fraction)};
                               auto effect = blastEffect(at, *player_, arena_);
                               if (effect.damage < player_->health)
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
    if (found == snapshot.players.end() || own < 1 || own > 2)
    {
        reset();
        return;
    }
    const bool boundary = arena_.map != snapshot.map || waitingForSnapshot_ || !player_ ||
                          round_ != snapshot.round || phase_ != snapshot.phase || !found->alive();
    if (arena_.map != snapshot.map || !player_)
        arena_ = Arena::create(snapshot.map);
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
    if (!boundary)
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
    PredictedCommand command{++sequence_, input};
    commands_.push_back(command);
    simulate(command, true);
    offset_.x *= .85f;
    offset_.y *= .85f;
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                                  [&](Bullet& b)
                                  {
                                      const auto from = b.position;
                                      if (b.weapon == WeaponKind::FireBoomerang)
                                      {
                                          if (b.age >= .55f)
                                              b.returning = true;
                                          if (b.returning)
                                          {
                                              const auto at = player_->weaponOrigin();
                                              const float distance = std::hypot(at.x - from.x, at.y - from.y);
                                              if (distance < 24)
                                                  return true;
                                              b.velocity = {(at.x - from.x) / distance * 700,
                                                            (at.y - from.y) / distance * 700};
                                          }
                                      }
                                      b.update(step);
                                      if (b.lifetime <= 0 || b.position.x < arena_.bounds.position.x ||
                                          b.position.x > arena_.bounds.size.x ||
                                          b.position.y < arena_.bounds.position.y ||
                                          b.position.y > arena_.bounds.size.y)
                                          return true;
                                      for (const auto& rect : arena_.platforms)
                                          if (collision::segmentHit(from, b.position, rect, Bullet::radius))
                                              return true;
                                      return false;
                                  }),
                   bullets_.end());
}

std::optional<GameEvent> Prediction::action(std::uint64_t sequence, bool jump, bool shoot, Vec2 aim,
                                            bool reload)
{
    if (!player_ || (phase_ != MatchPhase::Round && phase_ != MatchPhase::RoundEnd) || !player_->alive() ||
        actions_.size() >= 240)
        return {};
    actions_.push_back({sequence, sequence_ + 1, jump, shoot, reload, aim});
    if (shoot && visualCooldown_ <= 0)
    {
        auto visual = *player_;
        visual.requestAction(false, true, aim, sequence);
        if (auto bullet = visual.takeShot())
        {
            visualCooldown_ = visual.weapon.shotInterval;
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
    const Vec2 muzzle{prototype.position.x + direction.x * definition.barrel,
                      prototype.position.y + direction.y * definition.barrel};
    bool blocked = false;
    for (const auto& rect : arena_.platforms)
        if (collision::segmentHit(prototype.position, muzzle, rect, Bullet::radius))
            blocked = true;
    if (!blocked && !meleeWeapon(prototype.weapon))
    {
        unsigned pellet = 0;
        for (auto bullet : scatterShot(prototype))
        {
            bullet.id =
                std::numeric_limits<BulletId>::max() - static_cast<BulletId>(prototype.volley) * 8 - pellet++;
            bullet.position = muzzle;
            if (bullets_.size() < 256)
                bullets_.push_back(bullet);
        }
    }
    return GameEvent{
        0,      0,         prototype.shotSequence, round_,           EventKind::Shot, prototype.shooterId, 0,
        muzzle, direction, prototype.damage,       prototype.weapon, prototype.volley};
}

std::vector<GameEvent> Prediction::takeEvents()
{
    auto result = std::move(events_);
    events_.clear();
    return result;
}
} // namespace cubeshot
