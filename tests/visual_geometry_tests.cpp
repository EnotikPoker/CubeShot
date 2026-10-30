#include "game/Arena.h"
#include "game/Debris.h"
#include "game/Shape.h"
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

void contours()
{
    DamageShape shape;
    for (unsigned n = 0; n < 6; ++n)
    {
        expect(addEdgeChip(shape, {24, 0}, n), "Six chips can occupy different edge locations");
        const auto p = damageOutline(shape);
        expect(validPolygon(p) && polygonArea(p) >= 48 * 48 * .85f,
               "Damaged outline is valid and retains a dense centre");
        for (auto at : {Vec2{8, 8}, Vec2{40, 8}, Vec2{40, 40}, Vec2{8, 40}, Vec2{24, 24}})
            expect(containsPoint(p, at), "Centre never becomes perforated");
    }
    expect(!addEdgeChip(shape, {0, 0}, 99), "Chip budget is bounded");
    DamageShape cut;
    cut.count = 1;
    cut.revision = 1;
    cut.chips[0] = {0, 24, 12, 6};
    const auto p = damageOutline(cut);
    expect(!hitPolygon({22, -4}, {26, 0}, p, 0),
           "A ray through removed material does not hit the old rectangle");
    expect(hitPolygon({5, -20}, {5, 20}, p, 3).has_value(),
           "Remaining body still blocks a bullet with radius");
    auto hit = hitPolygon({24, -20}, {24, 20}, p, 3);
    expect(hit && *hit > .5f, "Radius-aware hit moves inward at the chipped edge");
    const auto triangles = triangulate(p);
    float area = 0;
    for (std::size_t i = 0; i < triangles.size(); i += 3)
        area += polygonArea({p[triangles[i]], p[triangles[i + 1]], p[triangles[i + 2]]});
    expect(std::abs(area - polygonArea(p)) < .01f, "Rendered triangles cover exactly the collision contour");
}

void malformedAndPose()
{
    expect(!validPolygon({{0, 0}, {30, 30}, {0, 30}, {30, 0}}), "Crossed outlines are rejected");
    expect(!validPolygon({{0, 0}, {48, 0}, {48, 48}, {24, 0}, {0, 48}}),
           "Non-adjacent touching edges are rejected");
    expect(!validPolygon({{0, 0}, {48, 0}, {48, 0}, {0, 48}}), "Duplicate vertices are rejected");
    Player p;
    p.velocity = {200, -400};
    addEdgeChip(p.surface, {0, 24}, 10);
    const Vec2 at{14, 17};
    const auto result = p.bodyLocal(p.bodyPoint(at));
    expect(std::hypot(result.x - at.x, result.y - at.y) < .001f,
           "Body-local hits survive animated pose transforms");
    expect(validPolygon(p.bodyContour()), "Animated pose retains a valid common collision/render outline");
}

void stressFragments()
{
    Player player;
    player.id = 2;
    player.position = {350, 380};
    player.health = 0;
    const auto arena = Arena::createDefault();
    for (unsigned seed = 0; seed < 80; ++seed)
    {
        player.surface = {};
        for (unsigned chip = 0; chip < 6; ++chip)
            addEdgeChip(player.surface,
                        {float((seed * 17 + chip * 7) % 48), float((seed * 11 + chip * 19) % 48)},
                        seed + chip);
        Debris debris;
        debris.spawn(player, {}, {1, 0}, WeaponKind::Rpg, 100);
        float area = 0;
        for (const auto& piece : debris.pieces())
        {
            expect(validPolygon(piece.outline),
                   "Seeded chipped-body fracture never produces malformed polygons");
            area += polygonArea(piece.outline);
        }
        expect(std::abs(area - polygonArea(player.bodyContour())) < .1f,
               "Seeded fractures conserve material, including concave edges");
    }
    Debris full;
    for (int n = 0; n < 12; ++n)
        full.spawn(player, {}, {1, 0}, WeaponKind::Rpg, 100);
    expect(full.pieces().size() == Debris::maxPieces,
           "Debris count reaches but never exceeds physical budget");
    for (int tick = 0; tick < 360; ++tick)
        full.update(1.f / 120, arena, {});
    for (const auto& piece : full.pieces())
        expect(std::isfinite(piece.position.y) && std::isfinite(piece.angle),
               "Full-budget physics remains finite");
    Bullet b;
    b.damage = 100;
    b.velocity = {800, 0};
    for (std::size_t n = 0; n < full.pieces().size(); ++n)
        expect(full.hit({0, true, n}, b, full.pieces()[n].position),
               "Even at the budget, depleted pieces must be destructible");
    full.update(1.f / 120, arena, {});
    expect(full.pieces().empty(), "Destroyed physical pieces are removed, leaving no invisible blockers");
}

void fractures()
{
    Player player;
    player.id = 2;
    player.position = {300, 392};
    player.health = 0;
    addEdgeChip(player.surface, {0, 12}, 42);
    for (auto entry : {std::pair<WeaponKind, unsigned>{WeaponKind::Pistol, 0},
                       {WeaponKind::Rpg, 6},
                       {WeaponKind::Sniper, 3},
                       {WeaponKind::Katana, 2},
                       {WeaponKind::FireBoomerang, 2},
                       {WeaponKind::Shotgun, 3}})
    {
        Debris debris;
        debris.spawn(player, {}, {1, 0}, entry.first, 100);
        expect(debris.pieces().size() == entry.second, "Weapon produces its own death fracture profile");
        float area = 0;
        if (entry.second)
        {
            expect(debris.corpses()[0].body.outline.empty(), "No ghost main body survives fragmentation");
            for (const auto& p : debris.pieces())
            {
                expect(validPolygon(p.outline) && !p.hull.empty(),
                       "Fragments have valid irregular physical shapes");
                area += polygonArea(p.outline);
            }
            expect(std::abs(area - polygonArea(damageOutline(player.surface))) < .1f,
                   "Fracture conserves the visible material area");
        }
        else
            expect(debris.corpses()[0].body.size.x == 48, "Ordinary death retains the full cube size");
        auto arena = Arena::createDefault();
        for (int tick = 0; tick < 600; ++tick)
            debris.update(1.f / 120, arena, {});
        for (const auto& p : debris.pieces())
            expect(std::isfinite(p.position.x) && std::isfinite(p.angle) && p.position.y < 440,
                   "Fragments settle within arena bounds");
    }
    Debris burned;
    burned.spawn(player, {}, {}, WeaponKind::Flamethrower, 3);
    expect(burned.corpses()[0].body.charred && burned.pieces().empty(), "Fire leaves an intact charred body");
}
} // namespace

int main()
{
    try
    {
        contours();
        malformedAndPose();
        fractures();
        stressFragments();
        std::cout << "Visual geometry passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
