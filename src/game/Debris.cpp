#include "game/Debris.h"
#include "game/Arena.h"
#include "game/Collision.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cubeshot
{
namespace
{
float dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}

Vec2 rotate(Vec2 v, float a)
{
    const float c = std::cos(a), s = std::sin(a);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

void collide(PhysicsPiece& p, Rect box, float dt)
{
    const auto right = rotate({1, 0}, p.angle), down = rotate({0, 1}, p.angle);
    const Vec2 centre{box.position.x + box.size.x / 2, box.position.y + box.size.y / 2};
    const Vec2 delta{p.position.x - centre.x, p.position.y - centre.y};
    float depth = std::numeric_limits<float>::max();
    Vec2 normal;
    for (auto axis : {Vec2{1, 0}, Vec2{0, 1}, right, down})
    {
        const float extent = std::abs(dot(axis, right)) * p.size.x / 2 +
                             std::abs(dot(axis, down)) * p.size.y / 2 + std::abs(axis.x) * box.size.x / 2 +
                             std::abs(axis.y) * box.size.y / 2;
        const float overlap = extent - std::abs(dot(delta, axis));
        if (overlap <= 0)
            return;
        if (overlap < depth)
        {
            depth = overlap;
            const float sign = dot(delta, axis) >= 0 ? 1.f : -1.f;
            normal = {axis.x * sign, axis.y * sign};
        }
    }
    p.position.x += normal.x * depth;
    p.position.y += normal.y * depth;
    const float speed = dot(p.velocity, normal);
    if (speed < 0)
    {
        p.velocity.x -= 1.16f * speed * normal.x;
        p.velocity.y -= 1.16f * speed * normal.y;
    }
    if (normal.y < -.5f)
    {
        p.velocity.x *= std::pow(.86f, dt * 120);
        p.spin = p.spin * std::pow(.94f, dt * 120) - std::sin(2 * p.angle) * dt * 16;
        if (std::abs(p.velocity.y) < 12)
            p.velocity.y = 0;
    }
}

void stepPiece(PhysicsPiece& p, float dt, const Arena& arena, const std::vector<Player>& players)
{
    p.velocity.y += Player::gravity * dt;
    p.velocity.x = std::clamp(p.velocity.x, -650.f, 650.f);
    p.velocity.y = std::clamp(p.velocity.y, -650.f, 900.f);
    p.spin = std::clamp(p.spin, -10.f, 10.f);
    p.position.x += p.velocity.x * dt;
    p.position.y += p.velocity.y * dt;
    p.angle = std::remainder(p.angle + p.spin * dt, 6.283185307f);
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& r : arena.platforms)
            collide(p, r, dt / 2);
    const float c = std::abs(std::cos(p.angle)), s = std::abs(std::sin(p.angle));
    Vec2 half{(c * p.size.x + s * p.size.y) / 2, (s * p.size.x + c * p.size.y) / 2};
    const auto& b = arena.bounds;
    const float x = std::clamp(p.position.x, b.position.x + half.x, b.position.x + b.size.x - half.x);
    const float y = std::clamp(p.position.y, b.position.y + half.y, b.position.y + b.size.y - half.y);
    if (x != p.position.x)
    {
        p.velocity.x *= -.2f;
        p.spin *= .5f;
    }
    if (y != p.position.y)
    {
        p.velocity.y *= -.15f;
        p.spin *= .7f;
    }
    p.position = {x, y};
    const Rect bounds{{x - half.x, y - half.y}, {half.x * 2, half.y * 2}};
    for (const auto& player : players)
        if (player.alive() && std::abs(player.velocity.x) > 1 && collision::overlaps(bounds, player.bounds()))
        {
            // One-way contact: a living player pushes remains without losing movement or support.
            p.velocity.x += (player.velocity.x * .85f - p.velocity.x) * std::min(1.f, dt * 30);
            p.spin += player.velocity.x * dt * .012f;
        }
}
} // namespace

Rect corpseCell(unsigned cell)
{
    return {{-24 + static_cast<float>(cell % 4) * 12, -16 + static_cast<float>(cell / 4) * 8}, {12, 8}};
}

Vec2 piecePoint(const PhysicsPiece& piece, Vec2 local)
{
    auto at = rotate(local, piece.angle);
    return {piece.position.x + at.x, piece.position.y + at.y};
}

std::optional<float> hitPiece(Vec2 from, Vec2 to, const PhysicsPiece& piece, Rect local, float radius)
{
    from = rotate({from.x - piece.position.x, from.y - piece.position.y}, -piece.angle);
    to = rotate({to.x - piece.position.x, to.y - piece.position.y}, -piece.angle);
    return collision::segmentHit(from, to, local, radius);
}

void Debris::blast(Vec2 at, float radius)
{
    for (auto& piece : pieces_)
    {
        const float dx = piece.position.x - at.x, dy = piece.position.y - at.y;
        const float distance = std::hypot(dx, dy);
        if (distance >= radius)
            continue;
        const float force = (1 - distance / radius) * 500;
        piece.velocity.x += (distance > .01f ? dx / distance : 0) * force;
        piece.velocity.y += (distance > .01f ? dy / distance : -1) * force - 100;
        piece.spin = std::clamp(piece.spin + dx * .03f, -10.f, 10.f);
    }
}

void Debris::clear()
{
    corpses_.clear();
    pieces_.clear();
}

void Debris::spawn(const Player& player, Vec2 velocity, Vec2 impact)
{
    Corpse corpse;
    corpse.body.id = nextId_++;
    corpse.body.owner = player.id;
    corpse.body.position = {player.position.x + player.size.x / 2, player.position.y + player.size.y / 2};
    corpse.body.size = {48, 32};
    corpse.body.velocity = {velocity.x * .35f + impact.x * 110, std::min(velocity.y * .2f, 0.f) - 90};
    corpse.body.spin = impact.x * 3;
    corpses_.push_back(corpse);
}

void Debris::update(float dt, const Arena& arena, const std::vector<Player>& players)
{
    for (auto& corpse : corpses_)
        if (corpse.cells)
            stepPiece(corpse.body, dt, arena, players);
    for (auto& piece : pieces_)
        stepPiece(piece, dt, arena, players);
}

std::optional<DebrisHit> Debris::trace(Vec2 from, Vec2 to) const
{
    std::optional<DebrisHit> result;
    float nearest = 2;
    for (std::size_t i = 0; i < corpses_.size(); ++i)
        for (unsigned cell = 0; cell < 16; ++cell)
            if (corpses_[i].cells & (1u << cell))
                if (const auto hit = hitPiece(from, to, corpses_[i].body, corpseCell(cell));
                    hit && *hit < nearest)
                {
                    nearest = *hit;
                    result = DebrisHit{*hit, false, i, cell};
                }
    for (std::size_t i = 0; i < pieces_.size(); ++i)
        if (const auto hit = hitPiece(from, to, pieces_[i],
                                      {{-pieces_[i].size.x / 2, -pieces_[i].size.y / 2}, pieces_[i].size});
            hit && *hit < nearest)
        {
            nearest = *hit;
            result = DebrisHit{*hit, true, i, 0};
        }
    return result;
}

void Debris::release(Corpse& corpse, unsigned cell, Vec2 impulse)
{
    if (!(corpse.cells & (1u << cell)))
        return;
    corpse.cells &= ~(1u << cell);
    if (pieces_.size() >= maxPieces)
        pieces_.erase(pieces_.begin());
    const auto local = corpseCell(cell);
    PhysicsPiece piece;
    piece.id = nextId_++;
    piece.owner = corpse.body.owner;
    piece.position = piecePoint(corpse.body, {local.position.x + 6, local.position.y + 4});
    piece.velocity = {corpse.body.velocity.x + impulse.x +
                          static_cast<float>(static_cast<int>(cell % 4) - 2) * 25,
                      corpse.body.velocity.y + impulse.y - 90 - static_cast<float>(cell / 4) * 12};
    piece.size = {11, 7};
    piece.angle = corpse.body.angle;
    piece.spin = corpse.body.spin + (cell % 2 ? 4.f : -4.f);
    pieces_.push_back(piece);
}

bool Debris::hit(const DebrisHit& target, const Bullet& bullet, Vec2 position)
{
    const float speed = std::hypot(bullet.velocity.x, bullet.velocity.y);
    const float force = std::clamp(static_cast<float>(bullet.damage) * 5, 45.f, 420.f);
    Vec2 impulse{(speed > .001f ? bullet.velocity.x / speed : 0) * force,
                 (speed > .001f ? bullet.velocity.y / speed : 0) * force - force * .2f};
    if (target.fragment)
    {
        auto& piece = pieces_[target.index];
        piece.velocity.x += impulse.x * 1.7f;
        piece.velocity.y += impulse.y * 1.7f;
        piece.spin += impulse.x * .035f;
        return false;
    }
    auto& corpse = corpses_[target.index];
    corpse.body.velocity.x += impulse.x;
    corpse.body.velocity.y += impulse.y;
    corpse.body.spin += (position.y - corpse.body.position.y) * impulse.x * -.003f;
    corpse.integrity = std::max(0, corpse.integrity - bullet.damage);
    release(corpse, target.cell, impulse);
    if (corpse.integrity == 0)
    {
        for (unsigned cell = 0; cell < 16; ++cell)
            release(corpse, cell, impulse);
        return true;
    }
    return false;
}

const std::vector<Corpse>& Debris::corpses() const
{
    return corpses_;
}

const std::vector<PhysicsPiece>& Debris::pieces() const
{
    return pieces_;
}
} // namespace cubeshot
