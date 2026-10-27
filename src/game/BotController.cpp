#include "game/BotController.h"
#include "game/Collision.h"
#include <algorithm>
#include <array>
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
    Vec2 muzzle{origin.x + direction.x * self.weapon.barrel(), origin.y + direction.y * self.weapon.barrel()};
    for (const auto& r : arena.platforms)
        if (collision::segmentHit(origin, muzzle, r, Bullet::radius))
            return false;
    Bullet b;
    b.position = muzzle;
    b.velocity = {direction.x * self.weapon.bulletSpeed, direction.y * self.weapon.bulletSpeed};
    b.acceleration = weaponDefinition(self.weapon.kind).gravity;
    b.lifetime = weaponDefinition(self.weapon.kind).lifetime;
    for (float t = 0; t < time; t += step)
    {
        auto start = b.position;
        b.update(std::min(step, time - t));
        if (b.lifetime < -1e-4f)
            return false;
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

constexpr std::size_t dodgeSteps = 80;

struct FlightStep
{
    Vec2 from, to;
    float barrier;
};

struct Threat
{
    BulletId id;
    std::array<FlightStep, dodgeSteps> path;
    std::size_t count = 0;
};

struct BodyPath
{
    std::array<Vec2, dodgeSteps + 1> points;
    std::size_t count = 0;
    Vec2 size;
};

Threat predictThreat(Bullet bullet, float horizon, const Arena& arena)
{
    Threat out;
    out.id = bullet.id;
    for (float elapsed = 0; elapsed < horizon && out.count < dodgeSteps; elapsed += step)
    {
        const auto from = bullet.position;
        bullet.update(std::min(step, horizon - elapsed));
        if (bullet.lifetime <= 0)
            break;
        const float barrier = terrainHit(from, bullet.position, arena);
        out.path[out.count++] = {from, bullet.position, barrier};
        if (barrier <= 1 || outside(bullet.position, arena))
            break;
    }
    return out;
}

BodyPath predictBody(Player self, const PlayerInput& input, float horizon, const Arena& arena)
{
    BodyPath out;
    out.points[0] = self.position;
    out.size = self.size;
    self.applyInput({input.moveDirection, false, false, input.aimDirection});
    self.requestAction(input.jump, false, input.aimDirection);
    const bool support = self.grounded;
    for (float elapsed = 0; elapsed < horizon && out.count < dodgeSteps; elapsed += step)
    {
        self.update(std::min(step, horizon - elapsed), arena, elapsed == 0 && support);
        out.points[++out.count] = self.position;
    }
    return out;
}

bool wouldHit(const Threat& threat, const BodyPath& body)
{
    const auto count = std::min(threat.count, body.count);
    for (std::size_t n = 0; n < count; ++n)
    {
        const auto& flight = threat.path[n];
        const auto hit =
            collision::segmentHit({flight.from.x - body.points[n].x, flight.from.y - body.points[n].y},
                                  {flight.to.x - body.points[n + 1].x, flight.to.y - body.points[n + 1].y},
                                  {{0, 0}, body.size}, Bullet::radius);
        if (hit && *hit < flight.barrier)
            return true;
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
    if (meleeWeapon(self.weapon.kind))
    {
        const Vec2 delta{center(target).x - origin.x, center(target).y - origin.y};
        const float range = std::hypot(delta.x, delta.y);
        if (range > self.weapon.barrel() + 20)
            return {};
        const float angle = std::atan2(delta.y, delta.x) + error;
        const Vec2 direction{std::cos(angle), std::sin(angle)};
        if (!clearShot(self, direction, 0, arena))
            return {};
        return BotAim{direction, 0};
    }
    if (self.weapon.kind == WeaponKind::Rpg && distance(origin, center(target)) < explosionRadius + 30)
        return {};
    float flight =
        std::max(.001f, (distance(origin, center(target)) - self.weapon.barrel()) / self.weapon.bulletSpeed);
    Vec2 delta;
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        Player predicted;
        predicted.position = target.position;
        predicted.velocity = target.velocity;
        predicted.size = target.size;
        predicted.update(flight, arena);
        delta = {predicted.position.x + target.size.x / 2 - origin.x,
                 predicted.position.y + target.size.y / 2 - origin.y -
                     weaponDefinition(self.weapon.kind).gravity * flight * flight / 2};
        flight =
            std::max(.001f, (std::hypot(delta.x, delta.y) - self.weapon.barrel()) / self.weapon.bulletSpeed);
        if (flight > weaponDefinition(self.weapon.kind).lifetime || !std::isfinite(flight))
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
    float rangeMax = tuning_.rangeMax, rangeMin = tuning_.rangeMin;
    if (meleeWeapon(self.weapon.kind))
    {
        rangeMin = 40;
        rangeMax = self.weapon.barrel() + 15;
    }
    else if (self.weapon.kind == WeaponKind::Flamethrower)
    {
        rangeMin = 100;
        rangeMax = 200;
    }
    else if (self.weapon.kind == WeaponKind::FireBoomerang)
    {
        rangeMin = 180;
        rangeMax = 310;
    }
    else if (self.weapon.kind == WeaponKind::Rpg)
        rangeMin = explosionRadius + 50;
    const float currentDistance = distance(self.weaponOrigin(), center(*target));
    if (currentDistance >= rangeMin && currentDistance <= rangeMax && botAim(self, *target, arena_))
        return self.position; // A useful firing position is a range, not a point to chase every frame.
    float best = std::numeric_limits<float>::max();
    Vec2 goal = self.position;

    struct Candidate
    {
        float cost;
        std::size_t index;
    };

    std::vector<Candidate> candidates;
    const auto& nodes = navigation_->nodes();
    candidates.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        Player candidate = self;
        candidate.position = nodes[index].position;
        const float range = distance(candidate.weaponOrigin(), center(*target));
        const float outside = std::max({rangeMin - range, range - rangeMax, 0.f});
        candidates.push_back({outside + .2f * distance(candidate.position, self.position), index});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](auto a, auto b) { return a.cost < b.cost; });
    std::size_t bestIndex = nodes.size();
    for (const auto& item : candidates)
    {
        // The visibility penalty is non-negative: later base costs cannot improve this result.
        if (item.cost > best)
            break;
        Player candidate = self;
        candidate.position = nodes[item.index].position;
        const float cost = item.cost + (botAim(candidate, *target, arena_) ? 0.f : 450.f);
        if (cost < best || (cost == best && item.index < bestIndex))
        {
            best = cost;
            bestIndex = item.index;
            goal = candidate.position;
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
    std::vector<Threat> known;
    known.reserve(perceived_.bullets.size());
    for (auto bullet : perceived_.bullets)
        if (bullet.shooterId != observation.self.id && exists(bullet.id) &&
            advanceKnownBullet(bullet, std::max(0.f, time_ - perceived_.time), arena_))
            known.push_back(predictThreat(bullet, tuning_.dodgeHorizon, arena_));
    auto risk = [&](const PlayerInput& candidate)
    {
        unsigned hits = 0;
        const auto body = predictBody(observation.self, candidate, tuning_.dodgeHorizon, arena_);
        for (const auto& b : known)
            hits += wouldHit(b, body) ? 1 : 0;
        return hits;
    };
    if (known.empty())
        return;
    const auto initialBody = predictBody(observation.self, input, tuning_.dodgeHorizon, arena_);
    unsigned initialRisk = 0;
    bool freshThreat = false;
    for (const auto& b : known)
        if (wouldHit(b, initialBody))
        {
            ++initialRisk;
            freshThreat = freshThreat || !jumpedBullets_.count(b.id);
        }
    if (!initialRisk)
        return;
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
    if (time_ == 0)
        aim_ = observation.self.aimDirection;
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
    bool alignedFire = false;
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
            alignedFire =
                std::abs(change) < .06f && clearShot(observation.self, aim_, solution->flightTime, arena_);
            if (observation.target && time_ >= nextShot_ && observation.self.weapon.cooldown <= 0 &&
                std::abs(change) < .06f && clearShot(observation.self, aim_, solution->flightTime, arena_))
            {
                input.shoot = true;
                nextShot_ =
                    time_ + std::max(tuning_.shotInterval * observation.self.weapon.shotInterval / .25f,
                                     observation.self.weapon.shotInterval);
            }
        }
    }
    if (observation.self.weapon.charging)
        input.shoot = true;
    if (observation.self.weapon.automatic())
        input.shoot = alignedFire && observation.target.has_value();
    input.triggerHeld = input.shoot;
    input.aimDirection = aim_;
    return input;
}
} // namespace cubeshot
