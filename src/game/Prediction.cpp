#include "game/Prediction.h"
#include "game/Collision.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace cubeshot {
void Prediction::reset() {
    player_.reset(); commands_.clear(); actions_.clear(); bullets_.clear();
    sequence_ = actionAck_ = 0; round_ = 0; phase_ = MatchPhase::Lobby; offset_ = {}; waitingForSnapshot_ = false;
}
void Prediction::simulate(const PredictedCommand& command) {
    auto input = command.input; input.jump = input.shoot = false;
    player_->applyInput(input);
    for (const auto& a : actions_) if (a.input == command.sequence)
        player_->requestAction(a.jump, a.shoot, a.aim, a.sequence);
    player_->update(step, arena_);
    // Advance weapon state during replay without spawning additional visuals.
    player_->takeShot();
}
void Prediction::reconcile(const WorldSnapshot& snapshot, PlayerId own) {
    const auto found = std::find_if(snapshot.players.begin(), snapshot.players.end(),
        [own](const Player& p) { return p.id == own; });
    if (found == snapshot.players.end() || own < 1 || own > 2) { reset(); return; }
    const bool boundary = waitingForSnapshot_ || !player_ || round_ != snapshot.round || phase_ != snapshot.phase || !found->alive();
    const auto old = player_ ? player_->position : found->position;
    const auto ack = snapshot.inputAck[own - 1];
    if (boundary) { sequence_ = ack; commands_.clear(); actions_.clear(); bullets_.clear(); offset_ = {}; }
    waitingForSnapshot_ = false;
    round_ = snapshot.round; phase_ = snapshot.phase; player_ = *found;
    sequence_ = std::max(sequence_, ack);
    actionAck_ = snapshot.actionAck[own - 1];
    while (!commands_.empty() && commands_.front().sequence <= ack) commands_.pop_front();
    while (!actions_.empty() && actions_.front().sequence <= actionAck_) actions_.pop_front();
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [&](const Bullet& b) {
        return b.shotSequence <= actionAck_;
    }), bullets_.end());
    for (const auto& c : commands_) simulate(c);
    if (!boundary) {
        offset_.x += old.x - player_->position.x; offset_.y += old.y - player_->position.y;
        if (std::hypot(offset_.x, offset_.y) > 96) offset_ = {};
    }
}
void Prediction::advance(PlayerInput input) {
    if (waitingForSnapshot_ || !player_ || phase_ != MatchPhase::Round || !player_->alive()) return;
    // Two seconds maximum; recover from the next authoritative snapshot.
    if (commands_.size() >= 240) { commands_.clear(); actions_.clear(); bullets_.clear(); waitingForSnapshot_ = true; return; }
    PredictedCommand command{++sequence_, input}; commands_.push_back(command);
    simulate(command);
    offset_.x *= .85f; offset_.y *= .85f;
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(), [&](Bullet& b) {
        const auto from = b.position; b.update(step);
        if (b.lifetime <= 0 || b.position.x < 0 || b.position.x > 960 || b.position.y < 0 || b.position.y > 540) return true;
        for (const auto& rect : arena_.platforms) if (collision::segmentHit(from, b.position, rect, Bullet::radius)) return true;
        return false;
    }), bullets_.end());
}
void Prediction::action(std::uint64_t sequence, bool jump, bool shoot, Vec2 aim) {
    if (!player_ || phase_ != MatchPhase::Round || !player_->alive() || actions_.size() >= 240) return;
    actions_.push_back({sequence, sequence_ + 1, jump, shoot, aim});
    if (shoot) {
        // Separate copy keeps the pending action and replay weapon state intact.
        auto visual = *player_; visual.requestAction(false, true, aim, sequence);
        if (auto bullet = visual.takeShot()) {
            bullet->id = std::numeric_limits<BulletId>::max() - sequence;
            const auto from = bullet->position;
            const Vec2 muzzle{from.x + aim.x / std::hypot(aim.x, aim.y) * Weapon::barrelLength,
                              from.y + aim.y / std::hypot(aim.x, aim.y) * Weapon::barrelLength};
            bool blocked = false;
            for (const auto& rect : arena_.platforms) if (collision::segmentHit(from, muzzle, rect, Bullet::radius)) blocked = true;
            if (!blocked) { bullet->position = muzzle; bullets_.push_back(*bullet); }
        }
    }
}
}
