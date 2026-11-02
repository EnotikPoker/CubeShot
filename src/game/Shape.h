#pragma once
#include "game/Types.h"
#include "game/Weapon.h"
#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace cubeshot
{
using Polygon = std::vector<Vec2>;

struct DamageMark
{
    Vec2 local;
    float width = 0, depth = 0;
    WeaponKind weapon = WeaponKind::Pistol;
    std::uint32_t shooter = 0, volley = 0;
};

struct DamageShape
{
    Polygon outline;
    std::vector<DamageMark> marks;
    unsigned revision = 0;
    bool charred = false;
    float scorch = 0;
};

struct ImpactContext
{
    Vec2 local, direction{1, 0};
    WeaponKind weapon = WeaponKind::Pistol;
    int damage = 20;
    std::uint32_t shooter = 0, volley = 0, seed = 0;
};

Polygon damageOutline(const DamageShape& shape);
Polygon carveImpact(DamageShape& shape, const ImpactContext& impact);
// Splits matching jagged boundaries; both returned polygons share the exact seam.
std::pair<Polygon, Polygon> fracturePolygon(const Polygon& polygon, Vec2 normal, float offset, unsigned seed,
                                            bool clean = false);
float polygonArea(const Polygon& polygon);
Vec2 polygonCentre(const Polygon& polygon);
bool containsPoint(const Polygon& polygon, Vec2 at);
std::optional<float> hitPolygon(Vec2 from, Vec2 to, const Polygon& polygon, float radius = 0);
std::vector<unsigned> triangulate(const Polygon& polygon);
Polygon clipPolygon(const Polygon& polygon, Vec2 normal, float distance);
Polygon convexHull(Polygon points);
bool validPolygon(const Polygon& polygon, unsigned maximum = 64);
} // namespace cubeshot
