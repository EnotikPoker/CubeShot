#include "game/Arena.h"
#include "game/Debris.h"
#include "game/Shape.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

namespace
{
void expect(bool b, const char* why)
{
    if (!b)
        throw std::runtime_error(why);
}

ImpactContext impact(Vec2 at, WeaponKind kind = WeaponKind::Pistol, unsigned seed = 42)
{
    return {at, {1, 0}, kind, weaponDefinition(kind).damage, 1, 1, seed};
}

void contours()
{
    DamageShape shape;
    const auto removed = carveImpact(shape, impact({0, 24}));
    expect(!removed.empty() && shape.marks.size() == 1 && shape.revision == 1,
           "One impact produces one local injury and actual detached material");
    auto p = damageOutline(shape);
    expect(validPolygon(p) && validPolygon(removed), "Carved material and remaining body are valid polygons");
    expect(std::abs(polygonArea(p) + polygonArea(removed) - 2304) < .05f, "Material is conserved when hit");
    for (auto v : removed)
        expect(v.x < 14 && v.y > 8 && v.y < 40,
               "Removed material stays near the actual contact, never moves to another edge");
    expect(!hitPolygon({-5, 23}, {0, 25}, p, 0),
           "A bullet crosses the removed material without hitting the old box");
    expect(hitPolygon({20, -10}, {20, 20}, p, 3).has_value(), "Remaining body blocks a radius-aware bullet");
    const auto triangles = triangulate(p);
    float area = 0;
    for (std::size_t n = 0; n < triangles.size(); n += 3)
        area += polygonArea({p[triangles[n]], p[triangles[n + 1]], p[triangles[n + 2]]});
    expect(std::abs(area - polygonArea(p)) < .05f, "Render triangulation matches the bullet outline");
    const auto previous = p;
    carveImpact(shape, impact({48, 10}, WeaponKind::Flamethrower));
    expect(shape.charred && shape.scorch > 0 && damageOutline(shape).size() == previous.size(),
           "Fire chars locally rather than inventing bite marks");
    DamageShape pellets;
    carveImpact(pellets, impact({0, 20}, WeaponKind::Shotgun, 5));
    carveImpact(pellets, impact({0, 25}, WeaponKind::Shotgun, 6));
    expect(pellets.marks.size() == 1, "Nearby contacts from one shotgun volley merge into one torn region");
    auto different = impact({48, 24}, WeaponKind::Shotgun, 7);
    different.volley = 2;
    carveImpact(pellets, different);
    expect(pellets.marks.size() == 2, "A distinct volley at another position makes its own local injury");
}

void malformedAndPose()
{
    expect(!validPolygon({{0, 0}, {30, 30}, {0, 30}, {30, 0}}), "Crossing contours are rejected");
    expect(!validPolygon({{0, 0}, {48, 0}, {48, 48}, {24, 0}, {0, 48}}),
           "Touching nonadjacent edges are rejected");
    expect(!validPolygon({{0, 0}, {48, 0}, {48, 0}, {0, 48}}), "Duplicate vertices are rejected");
    Player p;
    p.velocity = {200, -400};
    p.motionPhase = 1.2f;
    p.lean = .5f;
    const Vec2 at{14, 17};
    const auto result = p.bodyLocal(p.bodyPoint(at));
    expect(std::hypot(at.x - result.x, at.y - result.y) < .001f,
           "Pose and local impact coordinates are exact inverse transforms");
    expect(validPolygon(p.bodyContour()), "Animated body has a valid common collision and visual outline");
    p.grounded = true;
    p.velocity = {280, 0};
    p.motionPhase = 0;
    const auto first = p.bodyPoint({0, 0});
    p.motionPhase = 1.57f;
    expect(std::hypot(first.x - p.bodyPoint({0, 0}).x, first.y - p.bodyPoint({0, 0}).y) > .5f,
           "Walking visibly animates the body");
    expect(std::abs(p.bodyPoint({0, 48}).y - 48) < .001f, "Grounded animation keeps the feet on the support");
}

void fractures()
{
    Player player;
    player.id = 2;
    player.position = {300, 392};
    player.health = 0;
    for (auto entry : {std::pair<WeaponKind, unsigned>{WeaponKind::Pistol, 0},
                       {WeaponKind::Rpg, 6},
                       {WeaponKind::Sniper, 3},
                       {WeaponKind::Katana, 2},
                       {WeaponKind::FireBoomerang, 2},
                       {WeaponKind::Shotgun, 3}})
    {
        Debris debris;
        player.lastImpact = impact({0, 20}, entry.first);
        debris.spawn(player, {}, {1, 0}, entry.first, 100);
        const auto parts = std::count_if(debris.pieces().begin(), debris.pieces().end(),
                                         [](const auto& p) { return p.kind == PieceKind::Corpse; });
        expect(parts == entry.second, "Weapon-specific profiles retain correct physical material counts");
        if (parts)
        {
            float area = 0;
            for (const auto& part : debris.pieces())
                if (part.kind == PieceKind::Corpse)
                {
                    expect(validPolygon(part.outline), "Every torn piece has valid geometry");
                    expect(!part.hasEye || containsPoint(part.outline, part.eye),
                           "Attached eye stays inside its actual recentered fragment");
                    area += polygonArea(part.outline);
                }
            expect(std::abs(area - 2304) < .1f, "Body fracture conserves all body material");
        }
        int eyes = 0;
        for (const auto& c : debris.corpses())
            eyes += c.body.hasEye && !c.body.outline.empty();
        for (const auto& p : debris.pieces())
            eyes += p.kind == PieceKind::Eye || p.hasEye;
        expect(eyes == 1, "Exactly one attached or detached eye survives fracture");
        auto arena = Arena::createDefault();
        for (int tick = 0; tick < 500; ++tick)
            debris.update(1.f / 120, arena, {});
        for (const auto& p : debris.pieces())
            expect(std::isfinite(p.position.y) && std::isfinite(p.angle),
                   "Fragments and eyes remain physically finite");
    }
    Debris corpse;
    corpse.spawn(player, {}, {});
    auto eye = piecePoint(corpse.corpses()[0].body, corpse.corpses()[0].body.eye);
    Bullet b;
    b.damage = 20;
    b.velocity = {800, 0};
    corpse.hit({0, false, 0}, b, eye);
    expect(!corpse.corpses()[0].body.hasEye &&
               std::count_if(corpse.pieces().begin(), corpse.pieces().end(),
                             [](const auto& p) { return p.kind == PieceKind::Eye; }) == 1,
           "A hit near the corpse eye detaches it exactly once");
    corpse.hit({0, false, 0}, b, eye);
    expect(std::count_if(corpse.pieces().begin(), corpse.pieces().end(),
                         [](const auto& p) { return p.kind == PieceKind::Eye; }) == 1,
           "Repeated hits never duplicate an eye");
}

void stress()
{
    Player player;
    player.id = 2;
    player.position = {350, 380};
    player.health = 0;
    auto arena = Arena::createDefault();
    Polygon previous;
    for (unsigned seed = 1; seed <= 80; ++seed)
    {
        player.surface = {};
        for (unsigned n = 0; n < 10; ++n)
        {
            auto point = n % 4 == 0   ? Vec2{0, float((seed * 7 + n * 11) % 48)}
                         : n % 4 == 1 ? Vec2{48, float((seed * 13 + n * 7) % 48)}
                         : n % 4 == 2 ? Vec2{float((seed + n * 11) % 48), 0}
                                      : Vec2{float((seed * 3 + n * 7) % 48), 48};
            carveImpact(player.surface, impact(point, WeaponKind::Revolver, seed + n));
            auto outline = damageOutline(player.surface);
            expect(validPolygon(outline) && polygonArea(outline) >= 2304 * .7f,
                   "Repeated impacts respect area and vertex budgets");
            for (auto core : {Vec2{12, 12}, Vec2{36, 12}, Vec2{36, 36}, Vec2{12, 36}})
                expect(containsPoint(outline, core), "A living cube keeps a connected central body");
        }
        player.lastImpact = impact({0, 20}, WeaponKind::Rpg, seed);
        Debris debris;
        debris.spawn(player, {}, {1, 0}, WeaponKind::Rpg, 100);
        float area = 0;
        for (const auto& p : debris.pieces())
            if (p.kind == PieceKind::Corpse)
            {
                expect(validPolygon(p.outline), "Seeded fractures never make invalid polygons");
                area += polygonArea(p.outline);
            }
        expect(std::abs(area - polygonArea(damageOutline(player.surface))) < .15f,
               "Tearing a damaged cube preserves the remaining material");
        if (seed == 1)
            previous = debris.pieces().back().outline;
        if (seed == 2)
        {
            const auto& now = debris.pieces().back().outline;
            bool different = now.size() != previous.size();
            for (std::size_t n = 0; !different && n < now.size(); ++n)
                different = std::hypot(now[n].x - previous[n].x, now[n].y - previous[n].y) > .01f;
            expect(different, "Different impact seeds change the fracture pattern");
        }
    }
    Debris full;
    for (int n = 0; n < 14; ++n)
        full.spawn(player, {}, {1, 0}, WeaponKind::Rpg, 100);
    expect(full.pieces().size() == Debris::maxPieces, "Physical budget is bounded even with eyes");
    Bullet bullet;
    bullet.damage = 100;
    bullet.velocity = {800, 0};
    for (std::size_t n = 0; n < full.pieces().size(); ++n)
        if (full.pieces()[n].kind == PieceKind::Corpse)
            expect(full.hit({0, true, n}, bullet, full.pieces()[n].position),
                   "Full-budget chunks remain destructible");
    for (int tick = 0; tick < 200; ++tick)
        full.update(1.f / 120, arena, {});
    for (const auto& p : full.pieces())
        expect(std::isfinite(p.position.x) && p.kind == PieceKind::Eye,
               "Destroyed pieces never remain as invisible blockers");
}
} // namespace

int main()
{
    try
    {
        contours();
        malformedAndPose();
        fractures();
        stress();
        std::cout << "Local damage, pose, irregular fracture and eye tests passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
