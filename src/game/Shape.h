#pragma once
#include "game/Types.h"
#include <array>
#include <optional>
#include <vector>

namespace cubeshot
{
using Polygon = std::vector<Vec2>;

struct EdgeChip
{
    unsigned edge = 0;
    float along = 24, width = 10, depth = 5;
};

struct DamageShape
{
    std::array<EdgeChip, 6> chips{};
    unsigned count = 0, revision = 0;
    bool charred = false;
};

Polygon damageOutline(const DamageShape& shape);
bool addEdgeChip(DamageShape& shape, Vec2 localHit, unsigned seed);
float polygonArea(const Polygon& polygon);
Vec2 polygonCentre(const Polygon& polygon);
bool containsPoint(const Polygon& polygon, Vec2 at);
std::optional<float> hitPolygon(Vec2 from, Vec2 to, const Polygon& polygon, float radius = 0);
std::vector<unsigned> triangulate(const Polygon& polygon);
Polygon clipPolygon(const Polygon& polygon, Vec2 normal, float distance);
Polygon convexHull(Polygon points);
bool validPolygon(const Polygon& polygon, unsigned maximum = 32);
} // namespace cubeshot
