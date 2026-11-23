#include "game/AttackQueries.h"
#include "game/Arena.h"
#include "game/Collision.h"
#include "game/Player.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace cubeshot
{
std::optional<MeleeContact> meleeContact(Vec2 origin, Vec2 direction, float reach, const Player& target,
                                         const Arena& arena, float halfAngle)
{
    auto polygon = target.bodyContour();
    std::optional<MeleeContact> result;
    auto consider = [&](Vec2 point)
    {
        float dx = point.x - origin.x, dy = point.y - origin.y, n = std::hypot(dx, dy);
        if (n > reach ||
            (n > .001f && (dx * direction.x + dy * direction.y) / n < std::cos(halfAngle) - .0001f))
            return;
        for (auto& wall : arena.platforms)
            if (auto t = collision::segmentHit(origin, point, wall, 0); t && *t < .98f)
                return;
        if (!result || n < result->distance)
            result = MeleeContact{point, n};
    };
    for (std::size_t i = 0; i < polygon.size(); ++i)
    {
        Vec2 a{polygon[i].x + target.position.x, polygon[i].y + target.position.y},
            b{polygon[(i + 1) % polygon.size()].x + target.position.x,
              polygon[(i + 1) % polygon.size()].y + target.position.y};
        float dx = b.x - a.x, dy = b.y - a.y;
        float t = std::clamp(
            ((origin.x - a.x) * dx + (origin.y - a.y) * dy) / std::max(.0001f, dx * dx + dy * dy), 0.f, 1.f);
        consider(a);
        consider({a.x + dx * t, a.y + dy * t});
        for (float sign : {-1.f, 1.f})
        {
            float angle = std::atan2(direction.y, direction.x) + sign * halfAngle;
            Vec2 ray{std::cos(angle), std::sin(angle)};
            float den = dx * ray.y - dy * ray.x;
            if (std::abs(den) < .00001f)
                continue;
            float fraction = ((origin.x - a.x) * ray.y - (origin.y - a.y) * ray.x) / den;
            if (fraction >= 0 && fraction <= 1)
                consider({a.x + dx * fraction, a.y + dy * fraction});
        }
    }
    return result;
}

int healthDamage(int amount, float power, const Player& target, WeaponKind weapon, bool periodic)
{
    float multiplier = power * (1 - .1f * target.abilities.level(AbilityKind::ToughSkin));
    if (target.abilities.level(AbilityKind::FireResistance) &&
        (periodic || weapon == WeaponKind::Flamethrower))
        multiplier *= .5f;
    return std::max(1, static_cast<int>(std::lround(amount * multiplier)));
}

std::vector<ElectricContact> electricContacts(const Bullet& attack, const Arena& arena,
                                              const std::vector<Player>& players)
{
    std::vector<ElectricContact> result;
    Vec2 from = attack.position;
    float speed = std::max(.01f, std::hypot(attack.velocity.x, attack.velocity.y));
    Vec2 to{from.x + attack.velocity.x / speed * 600 * attack.rangeScale,
            from.y + attack.velocity.y / speed * 600 * attack.rangeScale};
    std::set<unsigned> visited;
    auto shot = attack;
    for (int hop = 0; hop < 3; ++hop)
    {
        float nearest = 2;
        const Player* victim = nullptr;
        const TerrainCell* conductor = nullptr;
        for (auto& solid : arena.platforms)
            if (auto hit = collision::segmentHit(from, to, solid, 0); hit && *hit < nearest)
            {
                Vec2 at{from.x + (to.x - from.x) * *hit, from.y + (to.y - from.y) * *hit};
                auto cell = arena.terrainAt(at, 1);
                if (cell && visited.count(cell->group))
                    continue;
                nearest = *hit;
                conductor = cell;
                victim = nullptr;
            }
        for (const auto& p : players)
            if (p.alive() && p.id != shot.shooterId)
                if (auto hit = collision::segmentHit(from, to, p.bounds(), 0); hit && *hit < nearest)
                {
                    nearest = *hit;
                    victim = &p;
                    conductor = nullptr;
                }
        Vec2 end =
            nearest <= 1 ? Vec2{from.x + (to.x - from.x) * nearest, from.y + (to.y - from.y) * nearest} : to;
        result.push_back({from, end, victim ? victim->id : 0, shot.damage, conductor != nullptr});
        if (victim)
        {
            break;
        }
        if (!conductor)
            break;
        unsigned group = conductor->group;
        Material material = conductor->material;
        if (material != Material::Metal)
            break;
        visited.insert(group);
        // Select the closest unvisited conductor or opponent with a clear connecting segment.
        Vec2 best{};
        float distance = 181;
        bool found = false;
        auto consider = [&](Vec2 candidate, unsigned ignored)
        {
            float len = std::hypot(candidate.x - end.x, candidate.y - end.y);
            if (len >= distance)
                return;
            for (auto& solid : arena.platforms)
                if (auto hit = collision::segmentHit(end, candidate, solid, 0); hit && *hit < .98f)
                {
                    Vec2 at{end.x + (candidate.x - end.x) * *hit, end.y + (candidate.y - end.y) * *hit};
                    auto cell = arena.terrainAt(at, 1);
                    if (!cell || (cell->group != group && cell->group != ignored))
                        return;
                }
            best = candidate;
            distance = len;
            found = true;
        };
        for (const auto& p : players)
            if (p.alive() && p.id != shot.shooterId)
                consider({p.position.x + p.size.x / 2, p.position.y + p.size.y / 2}, 0);
        for (auto& c : arena.terrain)
            if (c.health && c.material == Material::Metal && !visited.count(c.group))
                consider(
                    {c.bounds.position.x + c.bounds.size.x / 2, c.bounds.position.y + c.bounds.size.y / 2},
                    c.group);
        if (!found)
            break;
        from = end;
        to = best;
        shot.damage = std::max(1, static_cast<int>(std::lround(shot.damage * .65f)));
    }
    return result;
}
} // namespace cubeshot
