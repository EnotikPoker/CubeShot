#include "game/BotController.h"
#include "game/Collision.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cubeshot
{
namespace
{
constexpr float step = 1.f / 120, pi = 3.14159265358979323846f;

float distance(Vec2 a, Vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

Vec2 center(const BotTarget& p)
{
    return {p.position.x + p.size.x / 2, p.position.y + p.size.y / 2};
}

bool clearShot(const Player& self, Vec2 direction, float time, const Arena& arena)
{
    Vec2 origin = self.weaponOrigin();
    Vec2 muzzle{origin.x + direction.x * Weapon::barrelLength, origin.y + direction.y * Weapon::barrelLength};
    for (const auto& r : arena.platforms)
        if (collision::segmentHit(origin, muzzle, r, Bullet::radius))
            return false;
    Bullet b;
    b.position = muzzle;
    b.velocity = {direction.x * self.weapon.bulletSpeed, direction.y * self.weapon.bulletSpeed};
    for (float t = 0; t < time; t += step)
    {
        auto start = b.position;
        b.update(std::min(step, time - t));
        for (const auto& r : arena.platforms)
            if (collision::segmentHit(start, b.position, r, Bullet::radius))
                return false;
    }
    return true;
}
} // namespace

std::u32string_view botDifficultyName(BotDifficulty difficulty)
{
    switch (difficulty)
    {
        case BotDifficulty::Easy:
            return U"Лёгкая";
        case BotDifficulty::Hard:
            return U"Сложная";
        default:
            return U"Средняя";
    }
}

BotTuning botTuning(BotDifficulty difficulty)
{
    switch (difficulty)
    {
        case BotDifficulty::Easy:
            return {.5f, 8, .75f, 1.5f, 220, 420, .25f};
        case BotDifficulty::Hard:
            return {.18f, 2, .3f, 4, 300, 550, .65f};
        default:
            return {.3f, 4, .45f, 3, 280, 500, .45f};
    }
}

bool botLineOfSight(Vec2 from, Vec2 to, const Arena& arena)
{
    return std::none_of(arena.platforms.begin(), arena.platforms.end(),
                        [&](const Rect& r) { return collision::segmentHit(from, to, r).has_value(); });
}

BotObservation observeBot(const World& world, PlayerId self, PlayerId opponent)
{
    BotObservation result;
    for (const auto& p : world.players())
        if (p.id == self)
            result.self = p;
    const auto origin = result.self.weaponOrigin();
    for (const auto& p : world.players())
        if (p.id == opponent && p.alive())
        {
            bool visible = false;
            for (float height : {.2f, .5f, .8f})
                visible =
                    visible ||
                    botLineOfSight(origin, {p.position.x + p.size.x / 2, p.position.y + p.size.y * height},
                                   world.arena());
            if (visible)
                result.target = BotTarget{p.position, p.velocity, p.size, p.grounded};
        }
    for (const auto& b : world.bullets())
        if (b.shooterId != self && botLineOfSight(origin, b.position, world.arena()))
            result.bullets.push_back(b);
    return result;
}

std::optional<BotAim> botAim(const Player& self, const BotTarget& target, const Arena& arena, float error)
{
    const auto origin = self.weaponOrigin();
    float flight =
        std::max(.001f, (distance(origin, center(target)) - Weapon::barrelLength) / self.weapon.bulletSpeed);
    Vec2 delta;
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        Player predicted;
        predicted.position = target.position;
        predicted.velocity = target.velocity;
        predicted.size = target.size;
        predicted.update(flight, arena);
        delta = {predicted.position.x + target.size.x / 2 - origin.x,
                 predicted.position.y + target.size.y / 2 - origin.y - Bullet::gravity * flight * flight / 2};
        flight =
            std::max(.001f, (std::hypot(delta.x, delta.y) - Weapon::barrelLength) / self.weapon.bulletSpeed);
        if (flight > 3 || !std::isfinite(flight))
            return {};
    }
    const float angle = std::atan2(delta.y, delta.x) + error;
    Vec2 direction{std::cos(angle), std::sin(angle)};
    if (!clearShot(self, direction, flight, arena))
        return {};
    return BotAim{direction, flight};
}

BotController::BotController(Arena arena, BotDifficulty difficulty, std::uint32_t seed)
    : arena_(std::move(arena)), tuning_(botTuning(difficulty)), random_(seed),
      navigation_(BotNavigation::forMap(arena_.map))
{
    reset();
}

void BotController::reset()
{
    history_.clear();
    perceived_ = {};
    remembered_.reset();
    time_ = nextShot_ = nextAim_ = replan_ = stuck_ = avoidedUntil_ = nextPatrol_ = nextGoal_ = 0;
    seenAt_ = -100;
    error_ = 0;
    aim_ = {-1, 0};
    path_.clear();
    pathIndex_ = 0;
    traversalTick_ = avoidFrom_ = avoidTo_ = -1;
    progress_ = patrol_ = goal_ = {};
}

std::optional<Vec2> BotController::lastSeen() const
{
    if (!remembered_ || time_ - seenAt_ > tuning_.memory)
        return {};
    return remembered_->position;
}

Vec2 BotController::chooseGoal(const Player& self, const std::optional<BotTarget>& target)
{
    if (target)
    {
        float best = std::numeric_limits<float>::max();
        Vec2 goal = self.position;
        const float preferred = (tuning_.rangeMin + tuning_.rangeMax) / 2;
        for (const auto& node : navigation_->nodes())
        {
            const Vec2 origin{node.position.x + 24, node.position.y + 26.4f};
            float cost = std::abs(distance(origin, center(*target)) - preferred) +
                         .15f * distance(node.position, self.position);
            Player candidate = self;
            candidate.position = node.position;
            if (!botLineOfSight(origin, center(*target), arena_) || !botAim(candidate, *target, arena_))
                cost += 450;
            if (cost < best)
            {
                best = cost;
                goal = node.position;
            }
        }
        return goal;
    }
    if (lastSeen())
        return *lastSeen();
    if (time_ >= nextPatrol_ || distance(self.position, patrol_) < 24)
    {
        const auto& nodes = navigation_->nodes();
        patrol_ = nodes[std::uniform_int_distribution<std::size_t>(0, nodes.size() - 1)(random_)].position;
        nextPatrol_ = time_ + 4;
    }
    return patrol_;
}

PlayerInput BotController::navigate(const Player& self, Vec2 goal, float dt)
{
    PlayerInput input;
    const auto& nodes = navigation_->nodes();
    if (nodes.empty())
        return input;
    if (distance(self.position, progress_) >= 8)
    {
        progress_ = self.position;
        stuck_ = 0;
    }
    else if (!path_.empty())
        stuck_ += dt;
    if (stuck_ >= 1)
    {
        if (pathIndex_ < path_.size())
        {
            avoidFrom_ = static_cast<int>(path_[pathIndex_].from);
            avoidTo_ = static_cast<int>(path_[pathIndex_].to);
            avoidedUntil_ = time_ + 2;
        }
        path_.clear();
        stuck_ = 0;
        replan_ = 0;
        traversalTick_ = -1;
    }
    if (time_ >= replan_ && self.grounded && traversalTick_ < 0)
    {
        path_ = navigation_->route(self.position, goal, time_ < avoidedUntil_ ? avoidFrom_ : -1,
                                   time_ < avoidedUntil_ ? avoidTo_ : -1);
        pathIndex_ = 0;
        replan_ = time_ + .6f;
    }
    if (pathIndex_ >= path_.size())
    {
        if (std::abs(goal.y - self.position.y) < 20 && std::abs(goal.x - self.position.x) > 8)
            input.moveDirection = goal.x > self.position.x ? 1.f : -1.f;
        return input;
    }
    const auto& edge = path_[pathIndex_];
    const Vec2 start = nodes[edge.from].position, end = nodes[edge.to].position;
    if (!edge.jump && nodes[edge.from].surface == nodes[edge.to].surface)
    {
        const float dx = end.x - self.position.x;
        if (std::abs(dx) <= 5)
            ++pathIndex_;
        else
            input.moveDirection = dx > 0 ? 1.f : -1.f;
        return input;
    }
    if (traversalTick_ < 0)
    {
        const float dx = start.x - self.position.x;
        if (std::abs(dx) > .05f || std::abs(start.y - self.position.y) > 2 || !self.grounded)
        {
            input.moveDirection = std::clamp(dx / (Player::moveSpeed * step), -1.f, 1.f);
            return input;
        }
        traversalTick_ = 0;
    }
    input.jump = edge.jump && traversalTick_ == 0;
    input.moveDirection = traversalTick_ < edge.holdTicks ? static_cast<float>(edge.direction) : 0.f;
    ++traversalTick_;
    if ((self.grounded && traversalTick_ > 3 && traversalTick_ >= edge.durationTicks) ||
        traversalTick_ > edge.durationTicks + 45)
    {
        traversalTick_ = -1;
        ++pathIndex_;
        if (distance(self.position, end) > 35)
            replan_ = 0;
    }
    return input;
}

PlayerInput BotController::update(const BotObservation& observation, float dt)
{
    if (!(dt > 0) || !std::isfinite(dt) || !observation.self.alive())
        return {};
    time_ += dt;
    history_.push_back({time_, observation.target, observation.bullets});
    while (!history_.empty() && history_.front().time <= time_ - tuning_.reaction)
    {
        perceived_ = std::move(history_.front());
        history_.pop_front();
    }
    if (perceived_.target)
    {
        remembered_ = perceived_.target;
        seenAt_ = time_;
    }
    if (!lastSeen())
        remembered_.reset();
    if (time_ >= nextGoal_)
    {
        goal_ = chooseGoal(observation.self, perceived_.target);
        nextGoal_ = time_ + .6f;
    }
    auto input = navigate(observation.self, goal_, dt);
    // Delayed bullets, advanced to their estimated current position. No future human input is known.
    for (auto bullet : perceived_.bullets)
    {
        const float age = std::max(0.f, time_ - perceived_.time);
        bullet.position.x += bullet.velocity.x * age;
        bullet.position.y += bullet.velocity.y * age + Bullet::gravity * age * age / 2;
        bullet.velocity.y += Bullet::gravity * age;
        bool danger = false;
        for (float t = 0; t < tuning_.dodgeHorizon; t += step)
        {
            const auto start = bullet.position;
            bullet.update(step);
            bool blocked = false;
            for (const auto& platform : arena_.platforms)
                blocked = blocked ||
                          collision::segmentHit(start, bullet.position, platform, Bullet::radius).has_value();
            if (blocked)
                break;
            if (collision::segmentHit(start, bullet.position, observation.self.bounds(), Bullet::radius))
            {
                danger = true;
                break;
            }
        }
        if (danger)
        {
            input.jump = observation.self.grounded;
            input.moveDirection = bullet.velocity.x > 0 ? 1.f : -1.f;
            path_.clear();
            traversalTick_ = -1;
            replan_ = 0;
            break;
        }
    }
    if (time_ >= nextAim_)
    {
        error_ = std::uniform_real_distribution<float>(-tuning_.aimErrorDegrees,
                                                       tuning_.aimErrorDegrees)(random_) *
                 pi / 180;
        nextAim_ = time_ + .4f;
    }
    if (perceived_.target)
    {
        // Extrapolate the delayed observation, never the opponent's unobserved input.
        auto estimate = *perceived_.target;
        Player predicted;
        predicted.position = estimate.position;
        predicted.velocity = estimate.velocity;
        predicted.size = estimate.size;
        predicted.update(std::max(0.f, time_ - perceived_.time), arena_);
        estimate.position = predicted.position;
        estimate.velocity = predicted.velocity;
        estimate.grounded = predicted.grounded;
        auto solution = botAim(observation.self, estimate, arena_, error_);
        if (solution)
        {
            float current = std::atan2(aim_.y, aim_.x);
            const float desired = std::atan2(solution->direction.y, solution->direction.x);
            float change = std::remainder(desired - current, 2 * pi);
            current += std::clamp(change, -6.f * dt, 6.f * dt);
            aim_ = {std::cos(current), std::sin(current)};
            if (observation.target && time_ >= nextShot_ && observation.self.weapon.cooldown <= 0 &&
                std::abs(change) < .06f)
            {
                input.shoot = true;
                nextShot_ = time_ + tuning_.shotInterval;
            }
        }
    }
    input.aimDirection = aim_;
    return input;
}
} // namespace cubeshot
