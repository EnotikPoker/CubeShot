#include "game/ActiveAbilities.h"
#include "game/Arena.h"
#include "game/Collision.h"
#include "game/Player.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
bool validActive(ActiveKind k, bool empty)
{
    return unsigned(k) < unsigned(ActiveKind::Count) && (empty || k != ActiveKind::None);
}

bool validActiveLoadout(const ActiveLoadout& a)
{
    return validActive(a[0]) && validActive(a[1]) && (a[0] == ActiveKind::None || a[0] != a[1]);
}

const ActiveDefinition& activeDefinition(ActiveKind k)
{
    static const std::array<ActiveDefinition, 9> defs{
        {{U"Пусто", U"Получите способность между раундами", 0},
         {U"Рывок", U"Рывок на 200 пикселей к прицелу; стены останавливают", 6},
         {U"Телепорт", U"Перенос до 220 пикселей к прицелу, в том числе сквозь стены", 8},
         {U"Барьер", U"Поглощает 60 урона в течение 2 секунд", 12},
         {U"Лечение", U"+30 HP, снимает горение и кровотечение", 14},
         {U"Гарпун", U"10 урона и притягивание врага; дальность 600", 10},
         {U"Удар о землю", U"Быстрое падение; удар при посадке: 40 урона, радиус 110", 10},
         {U"Волна", U"15 урона и сильное отталкивание, радиус 150", 8},
         {U"Бомба", U"Взрыв через 1,1 с: до 60 урона, радиус 150; опасна владельцу", 12}}};
    return defs[validActive(k) ? unsigned(k) : 0];
}

std::optional<Vec2> blinkDestination(const Player& p, Vec2 dir, const Arena& a,
                                     const std::vector<Player>& others)
{
    float n = std::hypot(dir.x, dir.y);
    if (n < .001f)
        return {};
    for (int distance = 220; distance >= 20; --distance)
    {
        Vec2 at{p.position.x + dir.x / n * distance, p.position.y + dir.y / n * distance};
        Rect b{at, p.size};
        if (at.x < a.bounds.position.x || at.y < a.bounds.position.y ||
            at.x + p.size.x > a.bounds.position.x + a.bounds.size.x ||
            at.y + p.size.y > a.bounds.position.y + a.bounds.size.y)
            continue;
        bool blocked = false;
        for (auto& wall : a.platforms)
            blocked = blocked || collision::overlaps(b, wall);
        for (auto& other : others)
            if (other.id != p.id && other.alive())
                blocked = blocked || collision::overlaps(b, other.bounds());
        if (!blocked)
            return at;
    }
    return {};
}

bool activateLocal(Player& p, unsigned index, const Arena& arena, const std::vector<Player>& others)
{
    if (index >= 2 || !p.alive())
        return false;
    auto& slot = p.actives[index];
    if (!validActive(slot.kind, false) || slot.cooldown > .0001f)
        return false;
    switch (slot.kind)
    {
        case ActiveKind::Blink:
        {
            auto at = blinkDestination(p, p.aimDirection, arena, others);
            if (!at)
                return false;
            p.position = *at;
            p.grounded = collision::isSupported(p.bounds(), arena.bounds, arena.platforms);
            ++p.teleportRevision;
            break;
        }
        case ActiveKind::Dash:
            p.dashTime = .16f;
            p.dashDirection = p.aimDirection;
            break;
        case ActiveKind::Barrier:
            p.barrier = 60;
            p.barrierTime = 2;
            break;
        case ActiveKind::Heal:
            if (p.health == p.healthLimit() && p.burning <= 0 && p.bleeding <= 0)
                return false;
            p.heal(30);
            p.burning = p.burnTick = p.bleeding = p.bleedTick = 0;
            break;
        case ActiveKind::Slam:
            p.slamming = true;
            p.slamTime = 2;
            break;
        default:
            break;
    }
    slot.cooldown = activeDefinition(slot.kind).cooldown;
    return true;
}
} // namespace cubeshot
