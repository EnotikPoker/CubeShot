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

bool outside(Vec2 p, const Arena& arena)
{
    const auto& b = arena.bounds;
    return p.x < b.position.x + Bullet::radius || p.y < b.position.y + Bullet::radius ||
           p.x > b.position.x + b.size.x - Bullet::radius || p.y > b.position.y + b.size.y - Bullet::radius;
}

float terrainHit(Vec2 from, Vec2 to, const Arena& arena)
{
    float nearest = 2;
    for (const auto& r : arena.platforms)
        if (const auto hit = collision::segmentHit(from, to, r, Bullet::radius))
            nearest = std::min(nearest, *hit);
    return nearest;
}

bool advanceKnownBullet(Bullet& bullet, float age, const Arena& arena)
{
    for (float elapsed = 0; elapsed < age; elapsed += step)
    {
        const auto start = bullet.position;
        bullet.update(std::min(step, age - elapsed));
        if (bullet.lifetime <= 0 || terrainHit(start, bullet.position, arena) <= 1 ||
            outside(bullet.position, arena))
            return false;
    }
    return bullet.lifetime > 0;
}

bool wouldHit(Bullet bullet, Player self, const PlayerInput& input, float horizon, const Arena& arena)
{
    self.applyInput({input.moveDirection, false, false, input.aimDirection});
    self.requestAction(input.jump, false, input.aimDirection);
    const bool support = self.grounded;
    for (float elapsed = 0; elapsed < horizon && bullet.lifetime > 0; elapsed += step)
    {
        const float dt = std::min(step, horizon - elapsed);
        const auto from = bullet.position, bodyFrom = self.position;
        self.update(dt, arena, elapsed == 0 && support);
        bullet.update(dt);
        if (bullet.lifetime <= 0)
            return false;
        const auto hit =
            collision::segmentHit({from.x - bodyFrom.x, from.y - bodyFrom.y},
                                  {bullet.position.x - self.position.x, bullet.position.y - self.position.y},
                                  {{0, 0}, self.size}, Bullet::radius);
        const float barrier = terrainHit(from, bullet.position, arena);
        if (hit && *hit < barrier)
            return true;
        if (barrier <= 1 || outside(bullet.position, arena))
            return false;
    }
    return false;
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
            return {.5f, 8, .75f, 220, 420, .25f};
        case BotDifficulty::Hard:
            return {.18f, 2, .3f, 300, 550, .65f};
        default:
            return {.3f, 4, .45f, 280, 500, .45f};
    }
}

BotObservation observeBot(const World& world, PlayerId self, PlayerId opponent)
{
    BotObservation result;
    for (const auto& p : world.players())
    {
        if (p.id == self)
            result.self = p;
        if (p.id == opponent && p.alive())
            result.target = BotTarget{p.position, p.velocity, p.size, p.grounded};
    }
    for (const auto& b : world.bullets())
        if (b.shooterId != self)
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
      navigation_(BotNavigation::forArena(arena_))
{
    reset();
}

void BotController::reset()
{
    history_.clear();
    perceived_ = {};
    time_ = nextShot_ = nextAim_ = replan_ = stuck_ = avoidedUntil_ = nextGoal_ = 0;
    error_ = 0;
    aim_ = {-1, 0};
    path_.clear();
    pathIndex_ = 0;
    traversalTick_ = avoidFrom_ = avoidTo_ = -1;
    progress_ = goal_ = {};
    leftGround_ = false;
    dodgeUntil_ = dodgeMove_ = nextDodgeJump_ = 0;
    jumpedBullets_.clear();
}

Vec2 BotController::chooseGoal(const Player& self, const std::optional<BotTarget>& target)
{
    if (!target)
        return self.position; // Wait for reaction; do not start a random patrol before seeing the arena.
    const float currentDistance = distance(self.weaponOrigin(), center(*target));
    if (currentDistance >= tuning_.rangeMin && currentDistance <= tuning_.rangeMax &&
        botAim(self, *target, arena_))
        return self.position; // A useful firing position is a range, not a point to chase every frame.
    float best = std::numeric_limits<float>::max();
    Vec2 goal = self.position;
    for (const auto& node : navigation_->nodes())
    {
        Player candidate = self;
        candidate.position = node.position;
        const float range = distance(candidate.weaponOrigin(), center(*target));
        const float outside = std::max({tuning_.rangeMin - range, range - tuning_.rangeMax, 0.f});
        float cost = outside + .2f * distance(node.position, self.position);
        if (!botAim(candidate, *target, arena_))
            cost += 450;
        if (cost < best)
        {
            best = cost;
            goal = node.position;
        }
    }
    return goal;
}

void BotController::cancelRoute(bool failed)
{
    if (failed && pathIndex_ < path_.size())
    {
        avoidFrom_ = static_cast<int>(path_[pathIndex_].from);
        avoidTo_ = static_cast<int>(path_[pathIndex_].to);
        avoidedUntil_ = time_ + 2;
    }
    path_.clear();
    pathIndex_ = 0;
    traversalTick_ = -1;
    leftGround_ = false;
    stuck_ = 0;
    replan_ = 0;
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
    else if (pathIndex_ < path_.size())
        stuck_ += dt;
    if (stuck_ >= 1)
        cancelRoute(true);
    if (time_ >= replan_ && self.grounded && traversalTick_ < 0)
    {
        path_ = navigation_->route(self.position, goal, time_ < avoidedUntil_ ? avoidFrom_ : -1,
                                   time_ < avoidedUntil_ ? avoidTo_ : -1);
        pathIndex_ = 0;
        replan_ = time_ + .6f;
    }
    if (pathIndex_ >= path_.size())
    {
        path_.clear();
        pathIndex_ = 0;
        if (self.grounded && std::abs(goal.y - self.position.y) < 2 &&
            navigation_->nearest(self.position) == navigation_->nearest(goal))
            input.moveDirection =
                std::clamp((goal.x - self.position.x) / (Player::moveSpeed * step), -1.f, 1.f);
        else if (!self.grounded)
            input.moveDirection = self.velocity.x / Player::moveSpeed;
        return input;
    }
    const auto& edge = path_[pathIndex_];
    const Vec2 start = nodes[edge.from].position, end = nodes[edge.to].position;
    if (!edge.jump && nodes[edge.from].surface == nodes[edge.to].surface)
    {
        if (!self.grounded || std::abs(self.position.y - end.y) > 2)
        {
            cancelRoute(true);
            input.moveDirection = goal.x > self.position.x ? 1.f : -1.f;
            return input;
        }
        const float dx = end.x - self.position.x;
        if (std::abs(dx) <= .05f)
            ++pathIndex_;
        else
            input.moveDirection = std::clamp(dx / (Player::moveSpeed * step), -1.f, 1.f);
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
        traversalTick_ = 1;
        leftGround_ = false;
        input.jump = edge.jump;
        input.moveDirection = static_cast<float>(edge.direction);
        return input;
    }
    leftGround_ = leftGround_ || !self.grounded;
    if (self.grounded && (leftGround_ || (edge.jump && traversalTick_ >= 4)))
    {
        if (std::abs(self.position.y - end.y) > 2 || std::abs(self.position.x - end.x) > 35)
            cancelRoute(true);
        else
        {
            traversalTick_ = -1;
            leftGround_ = false;
            ++pathIndex_;
        }
        return input;
    }
    if (traversalTick_ > edge.durationTicks + 45)
    {
        cancelRoute(true);
        return input;
    }
    input.moveDirection = traversalTick_ < edge.holdTicks ? static_cast<float>(edge.direction) : 0.f;
    ++traversalTick_;
    return input;
}

void BotController::avoidBullets(const BotObservation& observation, PlayerInput& input)
{
    auto exists = [&](BulletId id)
    {
        return std::any_of(observation.bullets.begin(), observation.bullets.end(),
                           [&](const Bullet& b) { return b.id == id; });
    };
    for (auto it = jumpedBullets_.begin(); it != jumpedBullets_.end();)
        if (!exists(*it))
            it = jumpedBullets_.erase(it);
        else
            ++it;
    if (time_ < dodgeUntil_)
        return;
    std::vector<Bullet> known;
    for (auto bullet : perceived_.bullets)
        if (bullet.shooterId != observation.self.id && exists(bullet.id) &&
            advanceKnownBullet(bullet, std::max(0.f, time_ - perceived_.time), arena_))
            known.push_back(bullet);
    auto risk = [&](const PlayerInput& candidate)
    {
        unsigned hits = 0;
        for (const auto& b : known)
            hits += wouldHit(b, observation.self, candidate, tuning_.dodgeHorizon, arena_) ? 1 : 0;
        return hits;
    };
    const auto initialRisk = risk(input);
    if (!initialRisk)
        return;
    bool freshThreat = false;
    for (const auto& b : known)
        if (!jumpedBullets_.count(b.id) && wouldHit(b, observation.self, input, tuning_.dodgeHorizon, arena_))
            freshThreat = true;
    const bool canJump = observation.self.grounded && time_ >= nextDodgeJump_ && freshThreat;
    PlayerInput best = input;
    auto bestRisk = initialRisk;
    float bestCost = std::numeric_limits<float>::max();
    for (bool jump : {false, true})
        for (float move : {input.moveDirection, 0.f, -1.f, 1.f})
        {
            if (jump && !canJump)
                continue;
            PlayerInput candidate{move, jump, false, input.aimDirection};
            const auto danger = risk(candidate);
            const float cost = std::abs(move - input.moveDirection) + (jump ? 2.f : 0.f);
            if (danger < initialRisk && (danger < bestRisk || (danger == bestRisk && cost < bestCost)))
            {
                best = candidate;
                bestRisk = danger;
                bestCost = cost;
            }
        }
    if (bestRisk >= initialRisk)
        return; // A pointless jump is not an evasive action.
    cancelRoute(false);
    input = best;
    dodgeMove_ = best.moveDirection;
    dodgeUntil_ = time_ + (best.jump ? .35f : .18f);
    if (best.jump)
    {
        nextDodgeJump_ = time_ + .9f;
        for (const auto& b : known)
            jumpedBullets_.insert(b.id);
    }
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
    // Goals must be stable support positions. A cached mid-jump position can otherwise
    // map to a nearby platform and cause another jump after the threat has passed.
    if (time_ >= nextGoal_ && observation.self.grounded && time_ >= dodgeUntil_)
    {
        goal_ = chooseGoal(observation.self, perceived_.target);
        nextGoal_ = time_ + .6f;
    }
    PlayerInput input;
    if (time_ < dodgeUntil_)
        input.moveDirection = dodgeMove_;
    else
        input = navigate(observation.self, goal_, dt);
    avoidBullets(observation, input);
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
                std::abs(change) < .06f && clearShot(observation.self, aim_, solution->flightTime, arena_))
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
