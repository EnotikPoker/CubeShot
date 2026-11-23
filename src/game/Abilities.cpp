#include "game/Abilities.h"
#include <cmath>

namespace cubeshot
{
bool validAbility(AbilityKind kind)
{
    return static_cast<unsigned>(kind) < abilityCount;
}

const AbilityDefinition& abilityDefinition(AbilityKind kind)
{
    static const std::array<AbilityDefinition, abilityCount> catalog{
        {{U"Маленький", U"Размер и хитбокс уменьшены на 50%", 1},
         {U"Скорость", U"+30% к скорости движения, до трёх уровней", 3},
         {U"Прыжок", U"+30% к высоте прыжка, до трёх уровней", 3},
         {U"Здоровье", U"+50 к максимальному здоровью", maximumAbilityStacks},
         {U"Большие пули", U"+50% к размеру снарядов", 1},
         {U"Броня", U"+2 щита против выстрелов в каждом раунде", maximumAbilityStacks},
         {U"Перезарядка", U"+25% к скорости перезарядки", maximumAbilityStacks},
         {U"Магазин", U"+25% базового магазина, минимум один патрон", maximumAbilityStacks},
         {U"Поджигатель", U"Попадания поджигают противника", 1},
         {U"Джетпак", U"Удерживайте прыжок: 2 секунды тяги, посадка восстанавливает топливо", 1},
         {U"Отдача", U"+50% к физической отдаче оружия", 1},
         {U"Мощность", U"+15% урона здоровью за уровень, до трёх", 3},
         {U"Скорострельность", U"+12% темпа огня за уровень, до трёх", 3},
         {U"Разгон снарядов", U"+25% скорости пуль, болтов, ракет и гранат, до трёх", 3},
         {U"Дальнобойность", U"+25% дальности пуль, болтов, пламени и разрядов, до трёх", 3},
         {U"Кучность", U"Разброс уменьшается на 25% за уровень, до трёх", 3},
         {U"Двойной снаряд", U"Две обычные пули или болта, каждый с 65% урона", 1},
         {U"Рикошет", U"Один отскок обычных пуль и болтов, остаётся 70% урона", 1},
         {U"Прошивание", U"Пули и болты пробивают ещё одно хрупкое укрытие", 1},
         {U"Демонтаж", U"+50% урона материалам за уровень, до трёх", 3},
         {U"Взрывник", U"+20% радиуса взрывов за уровень, до двух", 2},
         {U"Вампиризм", U"Прямой урон врагу лечит на 15% снятого здоровья", 1},
         {U"Регенерация", U"4 HP/с за уровень после 3 с без урона, до трёх", 3},
         {U"Крепкая кожа", U"На 10% меньше входящего урона за уровень, до трёх", 3},
         {U"Огнестойкость", U"Вдвое меньше урона от огня и горения", 1},
         {U"Шипы", U"Возвращают 15% прямого урона, прошедшего в здоровье", 1},
         {U"Адреналин", U"Ниже 30% HP: +20% движения и +15% темпа огня", 1},
         {U"Двойной прыжок", U"Ещё один прыжок в воздухе; восстанавливается на опоре", 1},
         {U"Планирование", U"Предельная скорость падения уменьшена на 40%", 1},
         {U"Устойчивость", U"Вдвое меньше отбрасывание чужими атаками", 1},
         {U"Второй шанс", U"Раз за раунд: 25% HP, снятие огня и 0,5 с защиты от смерти", 1}}};
    return catalog[validAbility(kind) ? static_cast<unsigned>(kind) : 0];
}

unsigned Abilities::level(AbilityKind kind) const
{
    return validAbility(kind) ? levels[static_cast<unsigned>(kind)] : 0;
}

bool Abilities::canAdd(AbilityKind kind) const
{
    return validAbility(kind) && level(kind) < abilityDefinition(kind).maximum;
}

bool Abilities::add(AbilityKind kind)
{
    if (!canAdd(kind))
        return false;
    ++levels[static_cast<unsigned>(kind)];
    return true;
}

bool Abilities::valid() const
{
    for (unsigned i = 0; i < abilityCount; ++i)
        if (levels[i] > abilityDefinition(static_cast<AbilityKind>(i)).maximum)
            return false;
    return true;
}

std::vector<AbilityKind> Abilities::available() const
{
    std::vector<AbilityKind> out;
    for (unsigned i = 0; i < abilityCount; ++i)
        if (canAdd(static_cast<AbilityKind>(i)))
            out.push_back(static_cast<AbilityKind>(i));
    return out;
}

float Abilities::scale() const
{
    return level(AbilityKind::SmallBody) ? .5f : 1.f;
}

float Abilities::speed() const
{
    return 1 + .3f * level(AbilityKind::Speed);
}

float Abilities::jump() const
{
    return std::sqrt(1 + .3f * level(AbilityKind::Jump));
}

float Abilities::reload() const
{
    return 1 / (1 + .25f * level(AbilityKind::FastReload));
}

float Abilities::recoil() const
{
    return level(AbilityKind::Recoil) ? 1.5f : 1.f;
}

float Abilities::power() const
{
    return 1 + .15f * level(AbilityKind::Power);
}

float Abilities::fireRate() const
{
    return 1 + .12f * level(AbilityKind::FireRate);
}

float Abilities::projectileSpeed() const
{
    return 1 + .25f * level(AbilityKind::ProjectileSpeed);
}

float Abilities::range() const
{
    return 1 + .25f * level(AbilityKind::Range);
}

float Abilities::accuracy() const
{
    return std::pow(.75f, level(AbilityKind::Accuracy));
}

float Abilities::demolition() const
{
    return 1 + .5f * level(AbilityKind::Demolition);
}

float Abilities::blastRadius() const
{
    return 1 + .2f * level(AbilityKind::BlastRadius);
}

int Abilities::health() const
{
    return 100 + 50 * static_cast<int>(level(AbilityKind::Health));
}

unsigned Abilities::shields() const
{
    return 2 * level(AbilityKind::Armor);
}
} // namespace cubeshot
