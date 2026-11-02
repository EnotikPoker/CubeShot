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

namespace
{
unsigned hash(unsigned value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

Polygon clean(Polygon p)
{
    Polygon out;
    for (auto v : p)
        if (out.empty() || std::hypot(v.x - out.back().x, v.y - out.back().y) > .005f)
            out.push_back(v);
    if (out.size() > 1 && std::hypot(out.front().x - out.back().x, out.front().y - out.back().y) < .005f)
        out.pop_back();
    bool changed = true;
    while (changed && out.size() > 3)
    {
        changed = false;
        for (std::size_t n = 0; n < out.size(); ++n)
            if (std::abs(cross(out[(n + out.size() - 1) % out.size()], out[n], out[(n + 1) % out.size()])) <
                .0001f)
            {
                out.erase(out.begin() + static_cast<std::ptrdiff_t>(n));
                changed = true;
                break;
            }
    }
    return out;
}
} // namespace

Polygon damageOutline(const DamageShape& shape)
{
    return shape.outline.empty() ? Polygon{{0, 0}, {48, 0}, {48, 48}, {0, 48}} : shape.outline;
}

Polygon carveImpact(DamageShape& shape, const ImpactContext& impact)
{
    if (incendiaryWeapon(impact.weapon))
    {
        shape.charred = true;
        shape.scorch = std::min(1.f, shape.scorch + impact.damage * .035f);
        auto found = std::find_if(shape.marks.begin(), shape.marks.end(),
                                  [&](const auto& mark)
                                  {
                                      return incendiaryWeapon(mark.weapon) &&
                                             std::hypot(mark.local.x - impact.local.x,
                                                        mark.local.y - impact.local.y) < 20;
                                  });
        if (found != shape.marks.end())
            found->width = std::min(40.f, found->width + 2);
        else
        {
            if (shape.marks.size() >= 12)
                shape.marks.erase(shape.marks.begin());
            shape.marks.push_back({impact.local, 12, 0, impact.weapon, impact.shooter, impact.volley});
        }
        ++shape.revision;
        if (impact.weapon == WeaponKind::Flamethrower)
            return {};
    }
    const Polygon original = damageOutline(shape);
    float width = std::clamp(9.f + impact.damage * .26f, 10.f, 27.f),
          depth = std::clamp(4.f + impact.damage * .16f, 5.f, 12.f);
    if (impact.weapon == WeaponKind::Knife)
    {
        width = 8;
        depth = 6;
    }
    if (impact.weapon == WeaponKind::Katana)
    {
        width = 19;
        depth = 9;
    }
    if (impact.weapon == WeaponKind::Bat)
    {
        width = 24;
        depth = 8;
    }
    if (impact.weapon == WeaponKind::Shotgun)
    {
        width = 18;
        depth = 7;
    }
    DamageMark* merged = nullptr;
    for (auto& mark : shape.marks)
        if (mark.weapon == WeaponKind::Shotgun && impact.weapon == WeaponKind::Shotgun &&
            mark.volley == impact.volley && mark.shooter == impact.shooter &&
            std::hypot(mark.local.x - impact.local.x, mark.local.y - impact.local.y) < mark.width * .6f + 8)
        {
            width = std::min(30.f, mark.width + 3);
            depth = std::min(12.f, mark.depth + 1);
            merged = &mark;
            break;
        }
    std::vector<float> arc{0};
    float nearest = 1e9f, contactArc = 0;
    Vec2 normal;
    for (std::size_t n = 0; n < original.size(); ++n)
    {
        auto a = original[n], b = original[(n + 1) % original.size()];
        Vec2 edge{b.x - a.x, b.y - a.y};
        float length = std::hypot(edge.x, edge.y);
        const float t = std::clamp(((impact.local.x - a.x) * edge.x + (impact.local.y - a.y) * edge.y) /
                                       (length * length),
                                   0.f, 1.f);
        const float distance =
            std::hypot(a.x + edge.x * t - impact.local.x, a.y + edge.y * t - impact.local.y);
        if (distance < nearest)
        {
            nearest = distance;
            contactArc = arc.back() + length * t;
            normal = {-edge.y / length, edge.x / length};
        }
        arc.push_back(arc.back() + length);
    }
    // A clipped-away area may not be moved elsewhere to satisfy an HP threshold.
    if (nearest > 6)
        return {};
    const float perimeter = arc.back();
    auto at = [&](float distance)
    {
        distance = std::fmod(distance + perimeter * 4, perimeter);
        const auto n = std::min(
            original.size() - 1,
            static_cast<std::size_t>(std::upper_bound(arc.begin(), arc.end(), distance) - arc.begin() - 1));
        float t = (distance - arc[n]) / (arc[n + 1] - arc[n]);
        auto a = original[n], b = original[(n + 1) % original.size()];
        return Vec2{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    };
    for (unsigned attempt = 0; attempt < 6; ++attempt)
    {
        float begin = contactArc - width / 2, end = contactArc + width / 2;
        while (begin < 0)
        {
            begin += perimeter;
            end += perimeter;
        }
        Vec2 first = at(begin), last = at(end);
        Polygon cut{first};
        for (unsigned n = 1; n <= 3; ++n)
        {
            const float t = n / 4.f;
            const float noise =
                impact.weapon == WeaponKind::Katana ? 1.f : .72f + (hash(impact.seed + n * 17) % 100) * .004f;
            cut.push_back(
                {first.x + (last.x - first.x) * t + normal.x * depth * std::sin(t * 3.14159265f) * noise,
                 first.y + (last.y - first.y) * t + normal.y * depth * std::sin(t * 3.14159265f) * noise});
        }
        cut.push_back(last);
        Polygon kept = cut, removed{first};
        for (std::size_t n = 1; n <= original.size() * 3; ++n)
        {
            const auto index = n % original.size();
            float s = arc[index] + static_cast<float>(n / original.size()) * perimeter;
            if (s > begin + .001f && s < end - .001f)
                removed.push_back(original[index]);
        }
        removed.push_back(last);
        for (std::size_t n = cut.size() - 1; n > 0; --n)
            removed.push_back(cut[n - 1]);
        for (std::size_t n = 1; n <= original.size() * 4; ++n)
        {
            const auto index = n % original.size();
            float s = arc[index] + static_cast<float>(n / original.size()) * perimeter;
            if (s > end + .001f && s < begin + perimeter - .001f)
                kept.push_back(original[index]);
        }
        kept = clean(std::move(kept));
        removed = clean(std::move(removed));
        bool core = true;
        for (auto point : {Vec2{12, 12}, Vec2{36, 12}, Vec2{36, 36}, Vec2{12, 36}, Vec2{24, 24}})
            core = core && containsPoint(kept, point);
        if (core && validPolygon(kept) && validPolygon(removed) && polygonArea(kept) >= 48 * 48 * .7f &&
            std::abs(polygonArea(kept) + polygonArea(removed) - polygonArea(original)) < .05f)
        {
            shape.outline = std::move(kept);
            ++shape.revision;
            if (merged)
            {
                merged->local = {(merged->local.x + impact.local.x) / 2,
                                 (merged->local.y + impact.local.y) / 2};
                merged->width = width;
                merged->depth = std::max(merged->depth, depth);
            }
            else
            {
                if (shape.marks.size() >= 12)
                    shape.marks.erase(shape.marks.begin());
                shape.marks.push_back(
                    {impact.local, width, depth, impact.weapon, impact.shooter, impact.volley});
            }
            return removed;
        }
        width *= .78f;
        depth *= .68f;
    }
    return {};
}

std::pair<Polygon, Polygon> fracturePolygon(const Polygon& p, Vec2 normal, float offset, unsigned seed,
                                            bool straight)
{
    auto a = clipPolygon(p, normal, offset), b = clipPolygon(p, {-normal.x, -normal.y}, -offset);
    if (straight || a.empty() || b.empty())
        return {a, b};
    const Vec2 tangent{-normal.y, normal.x};
    float lo = 1e9f, hi = -1e9f;
    for (auto v : a)
        if (std::abs(dot(v, normal) - offset) < .001f)
        {
            lo = std::min(lo, dot(v, tangent));
            hi = std::max(hi, dot(v, tangent));
        }
    auto tear = [&](const Polygon& source, float amplitude)
    {
        Polygon result;
        for (std::size_t n = 0; n < source.size(); ++n)
        {
            auto from = source[n], to = source[(n + 1) % source.size()];
            result.push_back(from);
            if (std::abs(dot(from, normal) - offset) > .001f || std::abs(dot(to, normal) - offset) > .001f)
                continue;
            if (std::hypot(to.x - from.x, to.y - from.y) < 6)
                continue;
            for (unsigned j = 1; j < 4; ++j)
            {
                float t = j / 4.f;
                Vec2 v{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
                unsigned slot =
                    static_cast<unsigned>(std::lround((dot(v, tangent) - lo) / std::max(.01f, hi - lo) * 4));
                float wobble = (static_cast<int>(hash(seed + slot * 113) % 201) - 100) / 100.f * amplitude;
                v.x += normal.x * wobble;
                v.y += normal.y * wobble;
                result.push_back(v);
            }
        }
        return clean(std::move(result));
    };
    for (float scale : {1.f, .5f, .2f})
    {
        auto x = tear(a, std::min(4.f, (hi - lo) * .13f) * scale),
             y = tear(b, std::min(4.f, (hi - lo) * .13f) * scale);
        if (validPolygon(x) && validPolygon(y) &&
            std::abs(polygonArea(x) + polygonArea(y) - polygonArea(p)) < .05f)
            return {x, y};
    }
    return {a, b};
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
