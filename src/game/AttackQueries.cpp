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
                                         const Arena& arena)
{
    const auto outline = target.bodyContour();
    Vec2 at{};
    float nearest = 1e9f;
    for (std::size_t n = 0; n < outline.size(); ++n)
    {
        Vec2 a{outline[n].x + target.position.x, outline[n].y + target.position.y},
            b{outline[(n + 1) % outline.size()].x + target.position.x,
              outline[(n + 1) % outline.size()].y + target.position.y};
        float dx = b.x - a.x, dy = b.y - a.y;
        float t = std::clamp(
            ((origin.x - a.x) * dx + (origin.y - a.y) * dy) / std::max(.00001f, dx * dx + dy * dy), 0.f, 1.f);
        Vec2 point{a.x + dx * t, a.y + dy * t};
        float d = std::hypot(point.x - origin.x, point.y - origin.y);
        if (d < nearest)
        {
            nearest = d;
            at = point;
        }
    }
    if (nearest > reach ||
        (nearest > .001f &&
         ((at.x - origin.x) * direction.x + (at.y - origin.y) * direction.y) / nearest < .7f))
        return {};
    for (const auto& wall : arena.platforms)
        if (auto t = collision::segmentHit(origin, at, wall, 0); t && *t < .98f)
            return {};
    return MeleeContact{at, nearest};
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
