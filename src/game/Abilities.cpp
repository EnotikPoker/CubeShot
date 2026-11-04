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
         {U"Отдача", U"+50% к физической отдаче оружия", 1}}};
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

int Abilities::health() const
{
    return 100 + 50 * static_cast<int>(level(AbilityKind::Health));
}

unsigned Abilities::shields() const
{
    return 2 * level(AbilityKind::Armor);
}
} // namespace cubeshot
