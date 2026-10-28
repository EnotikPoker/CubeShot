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
    sequence_ = actionAck_ = 0;
    round_ = 0;
    phase_ = MatchPhase::Lobby;
    offset_ = {};
    waitingForSnapshot_ = false;
}

void Prediction::simulate(const PredictedCommand& command, bool visuals)
{
    auto input = command.input;
    input.jump = input.shoot = false;
    player_->applyInput(input);
    for (const auto& a : actions_)
        if (a.input == command.sequence)
            player_->requestAction(a.jump, a.shoot, a.aim, a.sequence);
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
    // Advance weapon state during replay without spawning additional visuals.
    if (auto bullet = player_->takeShot())
        if (visuals)
            if (auto event = presentShot(*bullet))
                events_.push_back(*event);
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

std::optional<GameEvent> Prediction::action(std::uint64_t sequence, bool jump, bool shoot, Vec2 aim)
{
    if (!player_ || (phase_ != MatchPhase::Round && phase_ != MatchPhase::RoundEnd) || !player_->alive() ||
        actions_.size() >= 240)
        return {};
    actions_.push_back({sequence, sequence_ + 1, jump, shoot, aim});
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
    if (!blocked)
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
