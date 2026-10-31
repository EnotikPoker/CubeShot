#include "game/Shape.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace cubeshot
{
namespace
{
float cross(Vec2 a, Vec2 b, Vec2 c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

float dot(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}
} // namespace

Polygon damageOutline(const DamageShape& shape)
{
    const Vec2 starts[]{{0, 0}, {48, 0}, {48, 48}, {0, 48}};
    const Vec2 axes[]{{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    Polygon out;
    out.reserve(4 + shape.count * 3);
    for (unsigned edge = 0; edge < 4; ++edge)
    {
        const auto s = starts[edge], t = axes[edge];
        const Vec2 inward{-t.y, t.x};
        auto point = [&](float along, float depth)
        { return Vec2{s.x + t.x * along + inward.x * depth, s.y + t.y * along + inward.y * depth}; };
        out.push_back(s);
        std::vector<EdgeChip> chips;
        for (unsigned i = 0; i < std::min(shape.count, 6u); ++i)
            if (shape.chips[i].edge == edge)
                chips.push_back(shape.chips[i]);
        std::sort(chips.begin(), chips.end(), [](auto a, auto b) { return a.along < b.along; });
        for (auto chip : chips)
        {
            out.push_back(point(chip.along - chip.width / 2, 0));
            out.push_back(point(chip.along, chip.depth));
            out.push_back(point(chip.along + chip.width / 2, 0));
        }
    }
    return out;
}

bool addEdgeChip(DamageShape& shape, Vec2 at, unsigned seed)
{
    if (shape.count >= 6)
        return false;
    const float distances[]{std::abs(at.y), std::abs(48 - at.x), std::abs(48 - at.y), std::abs(at.x)};
    const auto preferred = static_cast<unsigned>(std::min_element(distances, distances + 4) - distances);
    const float alongs[]{at.x, at.y, 48 - at.x, 48 - at.y};
    for (unsigned attempt = 0; attempt < 4; ++attempt)
    {
        const auto edge = (preferred + attempt) % 4;
        const float width = 9 + static_cast<float>((seed + shape.revision * 3) % 4);
        const float desired = std::clamp(alongs[edge], width / 2 + 1, 47 - width / 2);
        for (float along : {desired, 12.f, 36.f, 24.f})
        {
            bool free = along - width / 2 >= 1 && along + width / 2 <= 47;
            for (unsigned i = 0; i < shape.count; ++i)
                if (shape.chips[i].edge == edge &&
                    std::abs(shape.chips[i].along - along) < (shape.chips[i].width + width) / 2 + 2)
                    free = false;
            if (!free)
                continue;
            shape.chips[shape.count++] = {edge, along, width, 4 + static_cast<float>(seed % 3)};
            ++shape.revision;
            return true;
        }
    }
    return false;
}

float polygonArea(const Polygon& p)
{
    float area = 0;
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        const auto a = p[i], b = p[(i + 1) % p.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area / 2;
}

Vec2 polygonCentre(const Polygon& p)
{
    Vec2 centre{};
    float area = 0;
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        auto a = p[i], b = p[(i + 1) % p.size()];
        float c = a.x * b.y - b.x * a.y;
        area += c;
        centre.x += (a.x + b.x) * c;
        centre.y += (a.y + b.y) * c;
    }
    if (std::abs(area) < .001f)
        return {};
    return {centre.x / (3 * area), centre.y / (3 * area)};
}

bool containsPoint(const Polygon& p, Vec2 at)
{
    bool inside = false;
    for (std::size_t i = 0, j = p.size() ? p.size() - 1 : 0; i < p.size(); j = i++)
    {
        auto a = p[i], b = p[j];
        if ((a.y > at.y) != (b.y > at.y) && at.x < (b.x - a.x) * (at.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

std::optional<float> hitPolygon(Vec2 from, Vec2 to, const Polygon& p, float radius)
{
    if (p.size() < 3)
        return {};
    if (containsPoint(p, from))
        return 0;
    const Vec2 travel{to.x - from.x, to.y - from.y};
    float best = 2;
    auto offer = [&](float t)
    {
        if (t >= 0 && t <= 1)
            best = std::min(best, t);
    };
    const float speed2 = dot(travel, travel);
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        const auto a = p[i], b = p[(i + 1) % p.size()];
        const Vec2 edge{b.x - a.x, b.y - a.y};
        const float length = std::hypot(edge.x, edge.y);
        if (length < .0001f)
            continue;
        const Vec2 tangent{edge.x / length, edge.y / length}, normal{-tangent.y, tangent.x},
            delta{from.x - a.x, from.y - a.y};
        const float along = dot(delta, tangent), height = dot(delta, normal), change = dot(travel, normal);
        if (std::abs(height) <= radius && along >= 0 && along <= length)
            return 0;
        if (std::abs(change) > .000001f)
            for (float offset : {-radius, radius})
            {
                float t = (offset - height) / change;
                float u = along + dot(travel, tangent) * t;
                if (u >= 0 && u <= length)
                    offer(t);
            }
        if (radius > 0 && speed2 > .000001f)
            for (auto point : {a, b})
            {
                const Vec2 d{from.x - point.x, from.y - point.y};
                float c = dot(d, d) - radius * radius;
                if (c <= 0)
                    return 0;
                float q = dot(d, travel), disc = q * q - speed2 * c;
                if (disc >= 0)
                    offer((-q - std::sqrt(disc)) / speed2);
            }
    }
    return best <= 1 ? std::optional<float>{best} : std::nullopt;
}

std::vector<unsigned> triangulate(const Polygon& p)
{
    std::vector<unsigned> remaining(p.size()), out;
    std::iota(remaining.begin(), remaining.end(), 0u);
    if (polygonArea(p) < 0)
        std::reverse(remaining.begin(), remaining.end());
    unsigned guard = 0;
    while (remaining.size() > 2 && guard++ < 1024)
    {
        bool found = false;
        for (std::size_t i = 0; i < remaining.size(); ++i)
        {
            auto a = remaining[(i + remaining.size() - 1) % remaining.size()], b = remaining[i],
                 c = remaining[(i + 1) % remaining.size()];
            if (cross(p[a], p[b], p[c]) <= .0001f)
                continue;
            bool occupied = false;
            for (auto v : remaining)
                if (v != a && v != b && v != c && cross(p[a], p[b], p[v]) >= -.0001f &&
                    cross(p[b], p[c], p[v]) >= -.0001f && cross(p[c], p[a], p[v]) >= -.0001f)
                    occupied = true;
            if (occupied)
                continue;
            out.insert(out.end(), {a, b, c});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
            found = true;
            break;
        }
        if (!found)
            break;
    }
    return out;
}

Polygon clipPolygon(const Polygon& p, Vec2 normal, float distance)
{
    Polygon out;
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        auto a = p[i], b = p[(i + 1) % p.size()];
        float da = dot(a, normal) - distance, db = dot(b, normal) - distance;
        if (da <= .0001f)
            out.push_back(a);
        if ((da < 0 && db > 0) || (da > 0 && db < 0))
        {
            float t = da / (da - db);
            out.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
        }
    }
    Polygon clean;
    for (auto v : out)
        if (clean.empty() || std::hypot(v.x - clean.back().x, v.y - clean.back().y) > .01f)
            clean.push_back(v);
    if (clean.size() > 1 &&
        std::hypot(clean.front().x - clean.back().x, clean.front().y - clean.back().y) < .01f)
        clean.pop_back();
    return clean.size() >= 3 && std::abs(polygonArea(clean)) > 1 ? clean : Polygon{};
}

Polygon convexHull(Polygon points)
{
    if (points.size() < 3)
        return points;
    std::sort(points.begin(), points.end(),
              [](auto a, auto b) { return a.x == b.x ? a.y < b.y : a.x < b.x; });
    Polygon hull;
    for (int half = 0; half < 2; ++half)
    {
        std::size_t start = hull.size();
        for (auto v : points)
        {
            while (hull.size() >= start + 2 && cross(hull[hull.size() - 2], hull.back(), v) <= 0)
                hull.pop_back();
            hull.push_back(v);
        }
        hull.pop_back();
        std::reverse(points.begin(), points.end());
    }
    return hull;
}

bool validPolygon(const Polygon& p, unsigned maximum)
{
    if (p.size() < 3 || p.size() > maximum || polygonArea(p) <= 1)
        return false;
    for (auto v : p)
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || std::abs(v.x) > 96 || std::abs(v.y) > 96)
            return false;
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        const auto a = p[i], b = p[(i + 1) % p.size()];
        if (std::hypot(a.x - b.x, a.y - b.y) < .001f)
            return false;
        for (std::size_t j = i + 1; j < p.size(); ++j)
        {
            if (j == i + 1 || (i == 0 && j + 1 == p.size()))
                continue;
            const auto c = p[j], d = p[(j + 1) % p.size()];
            const float ac = cross(a, b, c), ad = cross(a, b, d), ca = cross(c, d, a), cb = cross(c, d, b);
            auto sign = [](float value) { return value > .00001f ? 1 : value < -.00001f ? -1 : 0; };
            if (sign(ac) * sign(ad) <= 0 && sign(ca) * sign(cb) <= 0 &&
                std::max(std::min(a.x, b.x), std::min(c.x, d.x)) <=
                    std::min(std::max(a.x, b.x), std::max(c.x, d.x)) + .00001f &&
                std::max(std::min(a.y, b.y), std::min(c.y, d.y)) <=
                    std::min(std::max(a.y, b.y), std::max(c.y, d.y)) + .00001f)
                return false;
        }
    }
    const auto triangles = triangulate(p);
    return triangles.size() == (p.size() - 2) * 3;
}
} // namespace cubeshot
