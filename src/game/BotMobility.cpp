#include "game/BotMobility.h"
#include "game/BodyPhysics.h"
#include "game/Collision.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace cubeshot
{
namespace
{
constexpr float step = 1.f / 120, pi = 3.14159265359f;

float distance(Vec2 a, Vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

float barrier(Vec2 from, Vec2 to, const Arena& arena)
{
    float t = 2;
    for (const auto& wall : arena.platforms)
        if (auto hit = collision::segmentHit(from, to, wall, Bullet::radius))
            t = std::min(t, *hit);
    return t;
}
} // namespace

BotMobility::BotMobility(Arena arena, bool hard)
    : arena_(std::move(arena)), hard_(hard), navigation_(BotNavigation::forArena(arena_))
{
}

void BotMobility::reset()
{
    validating_ = false;
    bestProgram_.reset();
    current_.reset();
    programs_.clear();
    best_.clear();
    playback_.clear();
    expected_.clear();
    next_ = frame_ = 0;
    retry_ = 0;
    lastSteps_ = 0;
}

float BotMobility::routeTime(Vec2 from, Vec2 to) const
{
    const auto path = navigation_->route(from, to);
    if (path.empty() && navigation_->nearest(from) != navigation_->nearest(to))
        return 1e6f;
    float ticks = 0;
    for (const auto& edge : path)
        ticks += edge.durationTicks;
    return std::max(ticks / 120, distance(from, to) / Player::moveSpeed);
}

BotMobility::Simulation BotMobility::simulateProgram(const Player& self, Program program) const
{
    Simulation s;
    s.player = self;
    s.opponent = opponent_;
    s.program = program;
    s.threats = threats_;
    s.burning = self.burning;
    s.burnClock = self.burnTick;
    float angle = std::atan2(self.aimDirection.y, self.aimDirection.x);
    s.align = static_cast<int>(
        std::ceil(std::abs(std::remainder(program.angle - angle, 2 * pi)) / (hard_ ? 10.f : 6.f) / step));
    return s;
}

void BotMobility::begin(const Player& self, Vec2 goal, const std::vector<Bullet>& threats,
                        const std::vector<FirePatch>& fires, const PlayerInput& baseline)
{
    root_ = self;
    goal_ = goal;
    threats_ = threats;
    fires_ = fires;
    best_.clear();
    bestPositions_.clear();
    programs_.clear();
    bestProgram_.reset();
    validating_ = false;
    next_ = 0;
    bestCost_ = 1e9f;
    bestHealth_ = 0;
    baseline_ = routeTime(self.position, goal);
    std::stable_sort(threats_.begin(), threats_.end(), [&](auto& a, auto& b)
                     { return distance(a.position, self.position) < distance(b.position, self.position); });
    if (threats_.size() > 16)
        threats_.resize(16);
    float dir = goal.x >= self.position.x ? 1.f : -1.f;
    if (self.weapon.kind == WeaponKind::Rpg)
        for (float move : {dir, -dir})
            for (float degrees : {30.f, 45.f, 60.f})
                for (float delay : {.08f, .12f, .2f})
                    programs_.push_back(
                        {move, dir > 0 ? pi - degrees * pi / 180 : degrees * pi / 180, delay, 0});
    else if (self.weapon.kind == WeaponKind::Shotgun)
        for (float move : {dir, 0.f, -dir})
            for (float angle : {pi / 2, pi / 2 + dir * .35f})
                for (float delay : {0.f, .08f, .16f})
                    programs_.push_back({move, angle, delay, 0});
    else
        for (float move : {dir, 0.f, -dir})
            for (float angle : {pi / 2, pi / 2 + dir * .35f})
                for (float duration : {.6f, 1.2f, 1.8f})
                    programs_.push_back({move, angle, 0, duration});
    programs_.insert(programs_.begin(),
                     {baseline.moveDirection, std::atan2(baseline.aimDirection.y, baseline.aimDirection.x), 0,
                      0, true, baseline.jump});
    baselineHealth_ = self.health;
    current_ = simulateProgram(root_, programs_[next_++]);
}

bool BotMobility::advance(Simulation& s)
{
    auto& p = s.player;
    const int tick = s.tick++;
    const int airborneTick = tick - s.align;
    Vec2 aim{std::cos(s.program.angle), std::sin(s.program.angle)};
    if (airborneTick < 0)
    {
        float initial = std::atan2(root_.aimDirection.y, root_.aimDirection.x);
        float change = std::remainder(s.program.angle - initial, 2 * pi);
        float angle = initial + change * float(tick + 1) / std::max(1, s.align);
        aim = {std::cos(angle), std::sin(angle)};
    }
    const bool mini = p.weapon.kind == WeaponKind::Minigun && !s.program.baseline;
    if (mini && p.weapon.spin >= .9f && s.poweredTick < 0)
        s.poweredTick = tick;
    const bool poweredDone = s.poweredTick >= 0 && (tick - s.poweredTick) * step >= s.program.duration;
    bool fire = !s.program.baseline && airborneTick >= 0 &&
                (mini ? !poweredDone : airborneTick == static_cast<int>(std::lround(s.program.delay / step)));
    PlayerInput input{airborneTick < 0 ? 0.f : s.program.move, airborneTick == 0 && s.program.jump, fire, aim,
                      fire};
    if (mini)
    {
        input.jump = !s.jumped && s.poweredTick >= 0 && p.grounded;
        if (input.jump)
            s.jumped = true;
        if (s.poweredTick < 0)
            input.moveDirection = 0;
    }
    if (mini && s.poweredTick >= 0 && s.program.move * (goal_.x - root_.position.x) > 0)
    {
        s.cleared = s.cleared || p.position.y <= goal_.y - 8 || goal_.y >= root_.position.y - 48;
        const float target = s.cleared ? goal_.x : root_.position.x;
        input.moveDirection =
            std::clamp((target - p.position.x) / (Player::moveSpeed * .15f) -
                           p.pushX * std::exp(-(p.grounded ? 10.f : 3.f) * step) / Player::moveSpeed,
                       -1.f, 1.f);
    }
    s.positions.push_back(p.position);
    s.inputs.push_back(input);
    p.applyInput(input);
    if (s.opponent)
    {
        std::vector<Player> bodies{p, *s.opponent};
        simulateBodies(bodies, arena_, step);
        p = bodies[0];
        s.opponent = bodies[1];
    }
    else
        p.update(step, arena_);
    // Existing threats and own rockets share flight, contact, blast and impulse formulas.
    auto advanceProjectiles = [&](std::vector<Bullet>& bullets, bool owned)
    {
        bullets.erase(
            std::remove_if(
                bullets.begin(), bullets.end(),
                [&](Bullet& b)
                {
                    Vec2 from = b.position;
                    if (b.weapon == WeaponKind::FireBoomerang)
                    {
                        if (b.age >= .55f && !b.returning)
                        {
                            b.returning = true;
                            b.hitMask = 0;
                        }
                        if (b.returning && s.opponent)
                        {
                            auto owner = s.opponent->weaponOrigin();
                            float d = distance(from, owner);
                            if (d < 24)
                                return true;
                            b.velocity = {(owner.x - from.x) / d * 700, (owner.y - from.y) / d * 700};
                        }
                    }
                    b.update(step);
                    float hit = barrier(from, b.position, arena_);
                    bool direct = false;
                    if (owned && s.opponent && s.opponent->alive())
                        if (auto t =
                                hitPolygon({from.x - s.opponent->position.x, from.y - s.opponent->position.y},
                                           {b.position.x - s.opponent->position.x,
                                            b.position.y - s.opponent->position.y},
                                           s.opponent->bodyContour(), Bullet::radius))
                            hit = std::min(hit, *t);
                    if (!owned && b.shooterId != p.id)
                    {
                        if (auto t = hitPolygon({from.x - p.position.x, from.y - p.position.y},
                                                {b.position.x - p.position.x, b.position.y - p.position.y},
                                                p.bodyContour(), Bullet::radius))
                            if (*t < hit &&
                                (b.weapon != WeaponKind::FireBoomerang || !(b.hitMask & (1u << p.id))))
                            {
                                hit = *t;
                                direct = true;
                            }
                    }
                    bool outside = b.position.x < arena_.bounds.position.x ||
                                   b.position.x > arena_.bounds.position.x + arena_.bounds.size.x ||
                                   b.position.y < arena_.bounds.position.y ||
                                   b.position.y > arena_.bounds.position.y + arena_.bounds.size.y;
                    if (hit <= 1 || b.lifetime <= 0 || outside)
                    {
                        Vec2 at{from.x + (b.position.x - from.x) * std::min(hit, 1.f),
                                from.y + (b.position.y - from.y) * std::min(hit, 1.f)};
                        if (b.weapon == WeaponKind::Rpg)
                        {
                            auto effect = blastEffect(at, p, arena_, direct ? p.id : 0);
                            p.applyImpulse(effect.impulse);
                            p.health -= effect.damage;
                            if (owned && effect.damage > 0)
                                s.blasted = true;
                        }
                        else if (direct)
                        {
                            p.health -= b.impactDamage();
                            if (incendiaryWeapon(b.weapon))
                                s.burning = 2.5f;
                        }
                        if (b.weapon == WeaponKind::FireBoomerang && b.lifetime > 0)
                        {
                            b.position = at;
                            if (direct)
                                b.hitMask |= 1u << p.id;
                            else if (!b.returning)
                            {
                                b.returning = true;
                                b.hitMask = 0;
                            }
                            return false;
                        }
                        return true;
                    }
                    return false;
                }),
            bullets.end());
    };
    advanceProjectiles(s.threats, false);
    advanceProjectiles(s.rockets, true);
    if (auto bullet = p.takeShot())
    {
        ++s.shots;
        if (bullet->weapon == WeaponKind::Rpg)
        {
            Vec2 from = bullet->position,
                 to{from.x + aim.x * p.weapon.barrel(), from.y + aim.y * p.weapon.barrel()};
            float t = barrier(from, to, arena_);
            if (t <= 1)
            {
                Vec2 at{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
                auto effect = blastEffect(at, p, arena_);
                p.applyImpulse(effect.impulse);
                p.health -= effect.damage;
                s.blasted = effect.damage > 0;
            }
            else
            {
                bullet->position = to;
                s.rockets.push_back(*bullet);
            }
        }
    }
    s.burning = std::max(0.f, s.burning - step);
    for (const auto& f : fires_)
    {
        Vec2 at{std::clamp(f.position.x, p.position.x, p.position.x + 48),
                std::clamp(f.position.y, p.position.y, p.position.y + 48)};
        if (f.life > tick * step && distance(at, f.position) < f.radius + 8)
            s.burning = 2.5f;
    }
    if (s.burning > 0)
    {
        s.burnClock += step;
        while (s.burnClock >= .25f)
        {
            s.burnClock -= .25f;
            p.health -= 3;
        }
    }
    else
        s.burnClock = 0;
    s.airborne = s.airborne || !p.grounded;
    if (airborneTick > 0 && s.airborne && p.grounded && s.shots && s.rockets.empty() &&
        (!mini || poweredDone))
        s.landed = true;
    return (!s.program.baseline && (s.landed || p.weapon.overheated)) || s.tick >= 480 || p.health < 40 ||
           !std::isfinite(p.position.x) || !std::isfinite(p.position.y);
}

bool BotMobility::control(const Player& self, Vec2 goal, const std::vector<Bullet>& threats,
                          const std::vector<FirePatch>& fires, PlayerInput& input, float dt,
                          const Player* opponent)
{
    lastSteps_ = 0;
    retry_ = std::max(0.f, retry_ - dt);
    if (!playback_.empty())
    {
        const bool freshDanger =
            std::any_of(threats.begin(), threats.end(),
                        [&](const Bullet& b)
                        {
                            const bool known = std::any_of(threats_.begin(), threats_.end(),
                                                           [&](const Bullet& old) { return old.id == b.id; });
                            return !known && b.shooterId != self.id &&
                                   distance(b.position, self.weaponOrigin()) <
                                       (b.weapon == WeaponKind::Rpg ? explosionRadius : 48.f) +
                                           std::hypot(b.velocity.x, b.velocity.y) * .2f;
                        });
        if (frame_ >= playback_.size() || distance(self.position, expected_[frame_]) > 40 ||
            self.health < 40 || self.weapon.overheated || self.weapon.kind != root_.weapon.kind ||
            freshDanger)
        {
            playback_.clear();
            expected_.clear();
            retry_ = 1.5f;
            return false;
        }
        input = playback_[frame_++];
        return true;
    }
    bool capable = self.weapon.kind == WeaponKind::Shotgun || self.weapon.kind == WeaponKind::Minigun ||
                   self.weapon.kind == WeaponKind::Rpg;
    if (!capable || !self.alive() || self.health < 40 || self.weapon.ammo <= 0 || self.weapon.reloading ||
        self.weapon.overheated)
    {
        current_.reset();
        programs_.clear();
        return false;
    }
    if (!current_)
    {
        if (retry_ > 0 || !self.grounded || (distance(self.position, goal) < 64 && threats.empty()))
            return false;
        opponent_ = opponent ? std::optional<Player>(*opponent) : std::nullopt;
        begin(self, goal, threats, fires, input);
    }
    if (distance(self.position, root_.position) > 48 || self.weapon.kind != root_.weapon.kind ||
        self.weapon.ammo != root_.weapon.ammo)
    {
        current_.reset();
        programs_.clear();
        retry_ = .2f;
        return false;
    }
    while (current_ && lastSteps_ < 960)
    {
        ++lastSteps_;
        if (!advance(*current_))
            continue;
        auto& s = *current_;
        if (s.program.baseline)
            baselineHealth_ = s.player.health;
        bool safe = !s.program.baseline && s.landed && s.player.health >= 40 && !s.player.weapon.overheated &&
                    s.player.weapon.heat < .95f && (self.weapon.kind != WeaponKind::Rpg || s.blasted);
        for (const auto& f : fires_)
            if (distance(s.player.position, {f.position.x - 24, f.position.y - 48}) < f.radius + 36 &&
                f.life > s.tick * step)
                safe = false;
        if (validating_)
        {
            validating_ = false;
            if (safe && distance(self.position, root_.position) < 4)
            {
                playback_ = s.inputs;
                expected_ = s.positions;
                frame_ = 0;
                current_.reset();
                input = playback_[frame_++];
                return true;
            }
            current_.reset();
            retry_ = .5f;
            return false;
        }
        if (safe)
        {
            float duration = s.tick * step, remaining = routeTime(s.player.position, goal_);
            float cost = duration + remaining;
            if ((cost < baseline_ * (hard_ ? .85f : .75f) ||
                 (hard_ && s.player.health - baselineHealth_ >= 10)) &&
                cost < bestCost_)
            {
                bestCost_ = cost;
                best_ = s.inputs;
                bestPositions_ = s.positions;
                bestHealth_ = s.player.health;
                bestProgram_ = s.program;
            }
        }
        if (next_ < programs_.size())
            current_ = simulateProgram(root_, programs_[next_++]);
        else
            current_.reset();
    }
    if (!current_)
    {
        retry_ = .8f;
        if (best_.empty())
            return false;
        // Re-run the winner from live ammo, spin, heat, velocity and pose next tick.
        // This validation is part of the same fixed per-update simulation budget.
        root_ = self;
        opponent_ = opponent ? std::optional<Player>(*opponent) : std::nullopt;
        validating_ = true;
        current_ = simulateProgram(self, *bestProgram_);
        input = {0, false, false, self.aimDirection, false};
        return true;
    }
    // A short, bounded preparation stop also prevents the root becoming stale.
    input = {0, false, false, self.aimDirection, false};
    return true;
}
} // namespace cubeshot
