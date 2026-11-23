#include "game/Collision.h"
#include "game/World.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace cubeshot
{
void World::electric(const Bullet& attack)
{
    Vec2 from = attack.position;
    float speed = std::max(.01f, std::hypot(attack.velocity.x, attack.velocity.y));
    Vec2 to{from.x + attack.velocity.x / speed * 600 * attack.rangeScale,
            from.y + attack.velocity.y / speed * 600 * attack.rangeScale};
    std::set<unsigned> visited;
    auto shot = attack;
    for (int hop = 0; hop < 3; ++hop)
    {
        float nearest = 2;
        Player* victim = nullptr;
        const TerrainCell* conductor = nullptr;
        for (auto& solid : arena_.platforms)
            if (auto hit = collision::segmentHit(from, to, solid, 0); hit && *hit < nearest)
            {
                Vec2 at{from.x + (to.x - from.x) * *hit, from.y + (to.y - from.y) * *hit};
                auto cell = arena_.terrainAt(at, 1);
                if (cell && visited.count(cell->group))
                    continue;
                nearest = *hit;
                conductor = cell;
                victim = nullptr;
            }
        for (auto& p : players_)
            if (p.alive() && p.id != shot.shooterId)
                if (auto hit = collision::segmentHit(from, to, p.bounds(), 0); hit && *hit < nearest)
                {
                    nearest = *hit;
                    victim = &p;
                    conductor = nullptr;
                }
        Vec2 end =
            nearest <= 1 ? Vec2{from.x + (to.x - from.x) * nearest, from.y + (to.y - from.y) * nearest} : to;
        emit(EventKind::ElectricArc, shot, end, victim ? victim->id : 0);
        events_.back().local = from;
        events_.back().surfaceRevision = hop;
        if (victim)
        {
            damage(*victim, shot, end, shot.damage);
            break;
        }
        if (!conductor)
            break;
        unsigned group = conductor->group;
        Material material = conductor->material;
        arena_.damageTerrain(end, 6, static_cast<int>(shot.damage * shot.terrainPower));
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
            for (auto& solid : arena_.platforms)
                if (auto hit = collision::segmentHit(end, candidate, solid, 0); hit && *hit < .98f)
                {
                    Vec2 at{end.x + (candidate.x - end.x) * *hit, end.y + (candidate.y - end.y) * *hit};
                    auto cell = arena_.terrainAt(at, 1);
                    if (!cell || (cell->group != group && cell->group != ignored))
                        return;
                }
            best = candidate;
            distance = len;
            found = true;
        };
        for (auto& p : players_)
            if (p.alive() && p.id != shot.shooterId)
                consider({p.position.x + p.size.x / 2, p.position.y + p.size.y / 2}, 0);
        for (auto& c : arena_.terrain)
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
}
} // namespace cubeshot
