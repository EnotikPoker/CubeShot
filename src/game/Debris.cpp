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

Vec2 rotate(Vec2 p, float a)
{
    float c = std::cos(a), s = std::sin(a);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}

Rect pieceBounds(const PhysicsPiece& p)
{
    float left = 1e6f, top = 1e6f, right = -1e6f, bottom = -1e6f;
    for (auto v : p.hull)
    {
        v = piecePoint(p, v);
        left = std::min(left, v.x);
        top = std::min(top, v.y);
        right = std::max(right, v.x);
        bottom = std::max(bottom, v.y);
    }
    return {{left, top}, {right - left, bottom - top}};
}

void collide(PhysicsPiece& p, Rect box, float dt)
{
    if (!collision::overlaps(pieceBounds(p), box))
        return;
    const Vec2 centre{box.position.x + box.size.x / 2, box.position.y + box.size.y / 2};
    float depth = 1e6f;
    Vec2 normal{};
    auto test = [&](Vec2 axis)
    {
        float length = std::hypot(axis.x, axis.y);
        if (length < .001f)
            return true;
        axis = {axis.x / length, axis.y / length};
        float lo = 1e6f, hi = -1e6f;
        for (auto v : p.hull)
        {
            float projection = dot(piecePoint(p, v), axis);
            lo = std::min(lo, projection);
            hi = std::max(hi, projection);
        }
        float at = dot(centre, axis),
              extent = std::abs(axis.x) * box.size.x / 2 + std::abs(axis.y) * box.size.y / 2;
        float overlap = std::min(hi - (at - extent), at + extent - lo);
        if (overlap <= 0)
            return false;
        if (overlap < depth)
        {
            depth = overlap;
            float sign = dot({p.position.x - centre.x, p.position.y - centre.y}, axis) >= 0 ? 1.f : -1.f;
            normal = {axis.x * sign, axis.y * sign};
        }
        return true;
    };
    if (!test({1, 0}) || !test({0, 1}))
        return;
    for (std::size_t i = 0; i < p.hull.size(); ++i)
    {
        auto a = p.hull[i], b = p.hull[(i + 1) % p.hull.size()];
        if (!test(rotate({a.y - b.y, b.x - a.x}, p.angle)))
            return;
    }
    p.position.x += normal.x * depth;
    p.position.y += normal.y * depth;
    float speed = dot(p.velocity, normal);
    if (speed < 0)
    {
        p.velocity.x -= 1.08f * speed * normal.x;
        p.velocity.y -= 1.08f * speed * normal.y;
    }
    if (normal.y < -.5f)
    {
        p.velocity.x *= std::pow(p.kind == PieceKind::Eye ? .97f : .84f, dt * 120);
        if (p.kind == PieceKind::Eye)
            p.spin = std::clamp(p.velocity.x / 3, -10.f, 10.f);
        p.spin *= std::pow(.87f, dt * 120);
        if (std::abs(p.velocity.y) < 18)
            p.velocity.y = 0;
    }
}

void stepPiece(PhysicsPiece& p, float dt, const Arena& arena, const std::vector<Player>& players)
{
    if (p.outline.empty())
        return;
    auto bounds = pieceBounds(p);
    for (const auto& player : players)
        if (player.alive() && std::abs(player.velocity.x) > 1 && collision::overlaps(bounds, player.bounds()))
        {
            p.velocity.x += (player.velocity.x * .8f - p.velocity.x) * std::min(1.f, dt * 24);
            p.spin += player.velocity.x * dt * .009f;
            p.sleep = 0;
        }
    if (p.sleep >= .5f)
        return;
    p.velocity.y += Player::gravity * dt;
    p.velocity.x = std::clamp(p.velocity.x, -650.f, 650.f);
    p.velocity.y = std::clamp(p.velocity.y, -650.f, 900.f);
    p.spin = std::clamp(p.spin, -10.f, 10.f);
    p.position.x += p.velocity.x * dt;
    p.position.y += p.velocity.y * dt;
    p.angle = std::remainder(p.angle + p.spin * dt, 6.283185307f);
    for (int pass = 0; pass < 2; ++pass)
        for (auto box : arena.platforms)
            collide(p, box, dt / 2);
    bounds = pieceBounds(p);
    const auto& a = arena.bounds;
    if (bounds.position.x < a.position.x)
    {
        p.position.x += a.position.x - bounds.position.x;
        p.velocity.x = std::abs(p.velocity.x) * .1f;
    }
    if (bounds.position.x + bounds.size.x > a.position.x + a.size.x)
    {
        p.position.x -= bounds.position.x + bounds.size.x - a.position.x - a.size.x;
        p.velocity.x = -std::abs(p.velocity.x) * .1f;
    }
    if (bounds.position.y < a.position.y)
    {
        p.position.y += a.position.y - bounds.position.y;
        p.velocity.y = std::abs(p.velocity.y) * .1f;
    }
    if (bounds.position.y + bounds.size.y > a.position.y + a.size.y)
    {
        p.position.y -= bounds.position.y + bounds.size.y - a.position.y - a.size.y;
        p.velocity.y = 0;
    }
    if (std::hypot(p.velocity.x, p.velocity.y) < 2 && std::abs(p.spin) < .05f)
    {
        p.sleep += dt;
        if (p.sleep >= .5f)
        {
            p.velocity = {};
            p.spin = 0;
        }
    }
    else
        p.sleep = 0;
}
} // namespace

Vec2 piecePoint(const PhysicsPiece& piece, Vec2 local)
{
    local = rotate(local, piece.angle);
    return {piece.position.x + local.x, piece.position.y + local.y};
}

void refreshPiece(PhysicsPiece& p)
{
    p.hull = convexHull(p.outline);
    if (p.hull.empty())
        return;
    float x = 0, y = 0;
    for (auto v : p.hull)
    {
        x = std::max(x, std::abs(v.x));
        y = std::max(y, std::abs(v.y));
    }
    p.size = {x * 2, y * 2};
}

std::optional<float> hitPiece(Vec2 from, Vec2 to, const PhysicsPiece& p, float radius)
{
    from = rotate({from.x - p.position.x, from.y - p.position.y}, -p.angle);
    to = rotate({to.x - p.position.x, to.y - p.position.y}, -p.angle);
    return hitPolygon(from, to, p.outline, radius);
}

void Debris::clear()
{
    corpses_.clear();
    pieces_.clear();
}

void Debris::blast(Vec2 at, float radius)
{
    for (auto& p : pieces_)
    {
        float dx = p.position.x - at.x, dy = p.position.y - at.y, d = std::hypot(dx, dy);
        if (d >= radius)
            continue;
        float force = (1 - d / radius) * 500;
        p.velocity.x += (d > .01f ? dx / d : 0) * force;
        p.velocity.y += (d > .01f ? dy / d : -1) * force - 100;
        p.spin = std::clamp(p.spin + dx * .03f, -10.f, 10.f);
        p.sleep = 0;
    }
}

void Debris::chip(const Player& player, const Polygon& material, Vec2 impulse, unsigned seed, bool fatal)
{
    if (pieces_.size() >= maxPieces)
    {
        auto old = std::find_if(pieces_.begin(), pieces_.end(),
                                [](const auto& p) { return p.kind == PieceKind::Chip; });
        if (old == pieces_.end())
            return;
        pieces_.erase(old);
    }
    PhysicsPiece p;
    p.id = nextId_++;
    p.owner = player.id;
    p.kind = PieceKind::Chip;
    p.life = 6;
    p.fractureSeed = seed;
    p.outline = material;
    for (auto& point : p.outline)
        point = player.bodyPoint(point);
    auto centre = polygonCentre(p.outline);
    p.position = {player.position.x + centre.x, player.position.y + centre.y};
    p.skinOffset = centre;
    for (auto& point : p.outline)
    {
        point.x -= centre.x;
        point.y -= centre.y;
    }
    p.velocity = {player.velocity.x * .3f + impulse.x * 170, player.velocity.y * .2f + impulse.y * 90 - 100};
    p.spin = static_cast<int>(seed % 13) - 6.f;
    const Vec2 anchor{player.aimDirection.x < 0 ? 11.f : 37.f, 15};
    if (containsPoint(material, anchor))
    {
        const auto point = player.bodyPoint(anchor);
        p.hasEye = true;
        p.eye = {point.x - centre.x, point.y - centre.y};
        p.kind = fatal ? PieceKind::Corpse : PieceKind::Chip;
        p.life = fatal ? -1.f : 6.f;
        if (player.lastImpact.weapon == WeaponKind::Rpg || player.lastImpact.weapon == WeaponKind::Sniper ||
            player.lastImpact.weapon == WeaponKind::Shotgun)
            detachEye(p, {impulse.x * 120, impulse.y * 90});
    }
    refreshPiece(p);
    if (pieces_.size() < maxPieces)
        pieces_.push_back(std::move(p));
}

void Debris::detachEye(PhysicsPiece& source, Vec2 impulse)
{
    if (!source.hasEye || pieces_.size() >= maxPieces)
        return;
    PhysicsPiece eye;
    eye.id = nextId_++;
    eye.owner = source.owner;
    eye.kind = PieceKind::Eye;
    eye.position = piecePoint(source, source.eye);
    eye.velocity = {source.velocity.x + impulse.x * 1.4f, source.velocity.y + impulse.y - 110};
    eye.spin = impulse.x * .02f;
    for (int n = 0; n < 10; ++n)
    {
        const float angle = n * 6.2831853f / 10;
        eye.outline.push_back({std::cos(angle) * 3.5f, std::sin(angle) * 3.5f});
    }
    refreshPiece(eye);
    source.hasEye = false;
    pieces_.push_back(std::move(eye));
}

void Debris::spawn(const Player& player, Vec2 velocity, Vec2 impact, WeaponKind cause, int volleyDamage)
{
    Corpse corpse;
    auto& p = corpse.body;
    p.id = nextId_++;
    p.owner = player.id;
    p.position = {player.position.x + 24, player.position.y + 24};
    auto pose = player;
    pose.velocity = velocity;
    p.outline = pose.bodyContour();
    for (auto& v : p.outline)
    {
        v.x -= 24;
        v.y -= 24;
    }
    p.velocity = {velocity.x * .35f + impact.x * 110, std::min(velocity.y * .2f, 0.f) - 60};
    p.spin = impact.x * 2.4f;
    p.fractureSeed = player.lastImpact.seed;
    p.cleanCut = cause == WeaponKind::Katana;
    p.hasEye = true;
    const auto anchor = pose.bodyPoint({player.aimDirection.x < 0 ? 11.f : 37.f, 15});
    p.eye = {anchor.x - 24, anchor.y - 24};
    p.hasEye = containsPoint(p.outline, p.eye);
    auto impactPoint = pose.bodyPoint(player.lastImpact.local);
    p.fractureOrigin = {impactPoint.x - 24, impactPoint.y - 24};
    p.integrity = 100;
    p.charred = incendiaryWeapon(cause) || player.surface.charred;
    corpse.cause = cause;
    refreshPiece(p);
    if (cause == WeaponKind::Rpg ||
        ((cause == WeaponKind::Sniper || cause == WeaponKind::Shotgun) && volleyDamage >= 80))
        detachEye(p, {impact.x * 110, impact.y * 75});
    unsigned count = cause == WeaponKind::Rpg                                              ? 6
                     : cause == WeaponKind::Sniper                                         ? 3
                     : (cause == WeaponKind::Katana || cause == WeaponKind::FireBoomerang) ? 2
                     : cause == WeaponKind::Shotgun && volleyDamage >= 60                  ? 3
                                                                                           : 1;
    if (count > 1)
    {
        split(p, count, {impact.x * 160, -100});
        p.outline.clear();
        p.hull.clear();
        corpse.integrity = 0;
    }
    else if (cause == WeaponKind::Rifle)
    {
        Bullet b;
        b.damage = 25;
        b.velocity = {impact.x * 100, impact.y * 100};
        corpses_.push_back(corpse);
        hit({0, false, corpses_.size() - 1}, b, {p.position.x + impact.x * 22, p.position.y + impact.y * 22});
        return;
    }
    corpses_.push_back(corpse);
}

void Debris::split(PhysicsPiece source, unsigned count, Vec2 impulse)
{
    std::vector<Polygon> parts{source.outline};
    while (parts.size() < count)
    {
        auto largest = std::max_element(
            parts.begin(), parts.end(),
            [&](const auto& a, const auto& b)
            {
                auto weight = [&](const auto& poly)
                {
                    auto c = polygonCentre(poly);
                    return polygonArea(poly) /
                           (1 +
                            std::hypot(c.x - source.fractureOrigin.x, c.y - source.fractureOrigin.y) * .025f);
                };
                return weight(a) < weight(b);
            });
        if (largest == parts.end() || polygonArea(*largest) < 70)
            break;
        auto centre = polygonCentre(*largest);
        Polygon a, b;
        // A half-plane can disconnect a concave chipped edge. Try a different
        // cut rather than joining disconnected material with a phantom bridge.
        for (unsigned attempt = 0; attempt < 16; ++attempt)
        {
            float angle = std::atan2(impulse.y, impulse.x) +
                          static_cast<float>((source.fractureSeed + source.id * 47 + parts.size() * 71 +
                                              source.revision * 19 + attempt * 29) %
                                             180) *
                              .01745329252f;
            Vec2 normal{std::cos(angle), std::sin(angle)};
            const float offset =
                dot(centre, normal) +
                static_cast<float>(static_cast<int>((source.fractureSeed + parts.size() * 11) % 9) - 4);
            auto partition = fracturePolygon(*largest, normal, offset,
                                             source.fractureSeed + static_cast<unsigned>(parts.size() * 337),
                                             source.cleanCut);
            a = std::move(partition.first);
            b = std::move(partition.second);
            if (validPolygon(a) && validPolygon(b))
                break;
            a.clear();
            b.clear();
        }
        if (a.empty() || b.empty())
            break;
        *largest = std::move(a);
        parts.push_back(std::move(b));
    }
    for (auto part : parts)
    {
        if (pieces_.size() >= maxPieces)
            break;
        auto centre = polygonCentre(part);
        PhysicsPiece p = source;
        p.id = nextId_++;
        for (auto& v : part)
        {
            v.x -= centre.x;
            v.y -= centre.y;
        }
        p.eye = {source.eye.x - centre.x, source.eye.y - centre.y};
        p.hasEye = source.hasEye && containsPoint(part, p.eye);
        p.fractureOrigin = {source.fractureOrigin.x - centre.x, source.fractureOrigin.y - centre.y};
        p.outline = std::move(part);
        p.position = piecePoint(source, centre);
        p.skinOffset = {source.skinOffset.x + centre.x, source.skinOffset.y + centre.y};
        const auto direction = rotate(centre, source.angle);
        p.velocity = {source.velocity.x + impulse.x + direction.x * 5,
                      source.velocity.y + impulse.y + direction.y * 4 - 20};
        p.spin = std::clamp(source.spin + (p.id % 2 ? 2.2f : -2.2f), -10.f, 10.f);
        p.integrity = std::max(8, static_cast<int>(polygonArea(p.outline) / 24));
        p.revision++;
        p.sleep = 0;
        refreshPiece(p);
        pieces_.push_back(std::move(p));
    }
}

void Debris::update(float dt, const Arena& arena, const std::vector<Player>& players)
{
    for (auto& piece : pieces_)
        if (piece.life > 0)
        {
            piece.life = std::max(0.f, piece.life - dt);
            if (piece.life == 0)
                piece.outline.clear();
        }
    pieces_.erase(
        std::remove_if(pieces_.begin(), pieces_.end(), [](const auto& p) { return p.outline.empty(); }),
        pieces_.end());
    for (auto& corpse : corpses_)
        stepPiece(corpse.body, dt, arena, players);
    for (auto& piece : pieces_)
        stepPiece(piece, dt, arena, players);
}

std::optional<DebrisHit> Debris::trace(Vec2 from, Vec2 to) const
{
    std::optional<DebrisHit> out;
    float nearest = 2;
    for (std::size_t i = 0; i < corpses_.size(); ++i)
        if (auto t = hitPiece(from, to, corpses_[i].body); t && *t < nearest)
        {
            nearest = *t;
            out = DebrisHit{*t, false, i};
        }
    for (std::size_t i = 0; i < pieces_.size(); ++i)
        if (auto t = pieces_[i].kind == PieceKind::Corpse ? hitPiece(from, to, pieces_[i])
                                                          : std::optional<float>{};
            t && *t < nearest)
        {
            nearest = *t;
            out = DebrisHit{*t, true, i};
        }
    return out;
}

bool Debris::hit(const DebrisHit& target, const Bullet& bullet, Vec2 position)
{
    if (target.fragment && pieces_.capacity() < maxPieces)
        pieces_.reserve(maxPieces);
    auto& p = target.fragment ? pieces_[target.index] : corpses_[target.index].body;
    if (p.outline.empty())
        return false;
    const float speed = std::hypot(bullet.velocity.x, bullet.velocity.y),
                force = std::clamp(bullet.damage * 4.f, 25.f, 360.f);
    Vec2 impulse{speed > .001f ? bullet.velocity.x / speed * force : 0,
                 (speed > .001f ? bullet.velocity.y / speed * force : 0) - force * .15f};
    const float massScale =
        target.fragment ? std::clamp(std::sqrt(2304.f / std::max(16.f, polygonArea(p.outline))), 1.f, 3.5f)
                        : 1.f;
    p.velocity.x += impulse.x * massScale;
    p.velocity.y += impulse.y * massScale;
    p.spin = std::clamp(p.spin - (position.y - p.position.y) * impulse.x * .002f, -10.f, 10.f);
    p.sleep = 0;
    if (p.hasEye)
    {
        const auto eye = piecePoint(p, p.eye);
        if (std::hypot(eye.x - position.x, eye.y - position.y) < 12 || bullet.damage >= 70)
            detachEye(p, impulse);
    }
    if (target.fragment)
    {
        p.integrity -= bullet.damage;
        if (p.integrity <= 0)
        {
            auto source = p;
            p.outline.clear();
            p.hull.clear();
            if (polygonArea(source.outline) > 100 && pieces_.size() + 2 <= maxPieces)
                split(source, 2, {impulse.x * .2f, impulse.y * .2f});
            // At the physical-piece limit the last material becomes cosmetic dust;
            // the cap must never make a zero-integrity fragment bulletproof.
            return true;
        }
        return false;
    }
    auto& corpse = corpses_[target.index];
    corpse.integrity = std::max(0, corpse.integrity - bullet.damage);
    p.integrity = corpse.integrity;
    if (corpse.integrity == 0)
    {
        auto source = p;
        p.hasEye = false;
        p.outline.clear();
        p.hull.clear();
        split(source, 4, {impulse.x * .3f, impulse.y * .3f});
        return true;
    }
    // Slice a small irregular chip off the struck edge instead of releasing a grid tile.
    auto local = rotate({position.x - p.position.x, position.y - p.position.y}, -p.angle);
    const float length = std::hypot(local.x, local.y);
    if (length > 1 && pieces_.size() < maxPieces)
    {
        Vec2 normal{local.x / length, local.y / length};
        float far = -1e6f;
        for (auto v : p.outline)
            far = std::max(far, dot(v, normal));
        float cut = far - std::clamp(bullet.damage * .18f, 2.f, 6.f);
        auto partition =
            fracturePolygon(p.outline, normal, cut, p.fractureSeed + p.revision * 193, p.cleanCut);
        auto kept = std::move(partition.first), chip = std::move(partition.second);
        if (validPolygon(kept) && validPolygon(chip) && polygonArea(kept) > 300)
        {
            auto source = p;
            source.outline = std::move(chip);
            source.hasEye = p.hasEye && containsPoint(source.outline, p.eye);
            if (source.hasEye)
                p.hasEye = false;
            p.outline = std::move(kept);
            ++p.revision;
            refreshPiece(p);
            split(source, 1, {impulse.x * .35f, impulse.y * .35f});
        }
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
