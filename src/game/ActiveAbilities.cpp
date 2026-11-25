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
    static const std::array<ActiveDefinition, 15> defs{
        {{U"Пусто", U"Получите способность между раундами", 0},
         {U"Рывок", U"Рывок на 200 пикселей к прицелу; стены останавливают", 6},
         {U"Телепорт", U"Перенос до 220 пикселей к прицелу, в том числе сквозь стены", 8},
         {U"Барьер", U"Поглощает 60 урона в течение 2 секунд", 12},
         {U"Лечение", U"+30 HP, снимает горение и кровотечение", 14},
         {U"Гарпун", U"10 урона и притягивание врага; дальность 600", 10},
         {U"Удар о землю", U"Быстрое падение; удар при посадке: 40 урона, радиус 110", 10},
         {U"Волна", U"15 урона и сильное отталкивание, радиус 150", 8},
         {U"Бомба", U"Взрыв через 1,1 с: до 60 урона, радиус 150; опасна владельцу", 12},
         {U"Парирование", U"0,35 с защиты спереди: отражает пули и блокирует ближний удар", 7},
         {U"Ледяная граната", U"15 урона, радиус 130; замедление 25% на 2 с; опасна владельцу", 10},
         {U"Мина", U"На опоре: взвод 0,6 с, 45 урона в радиусе 130; можно расстрелять", 12},
         {U"Силовой экран", U"90 HP на 3 с; пропускает игроков, блокирует атаки с обеих сторон", 12},
         {U"Форсаж", U"На 4 с: +25% темпа атак и скорости перезарядки", 14},
         {U"Очищение", U"Снимает огонь, кровотечение и замедление; защита 20% на 1,5 с", 10}}};
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
        case ActiveKind::Parry:
            p.parry = .35f;
            break;
        case ActiveKind::Overdrive:
            p.overdrive = 4;
            break;
        case ActiveKind::Cleanse:
            p.burning = p.burnTick = p.bleeding = p.bleedTick = p.slow = 0;
            p.cleanse = 1.5f;
            break;
        case ActiveKind::Mine:
            if (!p.grounded)
                return false;
            break;
        case ActiveKind::Screen:
        {
            Vec2 at{p.position.x + p.size.x / 2 + p.aimDirection.x * 120 - 5, p.position.y + p.size.y - 112};
            Rect box{at, {10, 112}};
            if (at.x < arena.bounds.position.x || at.y < arena.bounds.position.y ||
                at.x + 10 > arena.bounds.position.x + arena.bounds.size.x ||
                at.y + 112 > arena.bounds.position.y + arena.bounds.size.y)
                return false;
            for (auto wall : arena.platforms)
                if (collision::overlaps(box, wall))
                    return false;
            for (auto body : others)
                if (body.alive() && collision::overlaps(box, body.bounds()))
                    return false;
            break;
        }
        case ActiveKind::Slam:
            p.slamming = true;
            p.slamTime = 2;
            break;
        default:
            break;
    }
    p.spawnProtection = 0;
    if (p.abilities.level(AbilityKind::Resonance))
        p.resonance = 2;
    p.actives[1 - index].cooldown =
        std::max(0.f, p.actives[1 - index].cooldown - .5f * p.abilities.level(AbilityKind::Transfer));
    slot.cooldown = activeDefinition(slot.kind).cooldown;
    return true;
}
} // namespace cubeshot
