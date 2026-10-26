#include "game/Weapon.h"
#include <algorithm>
#include <array>

namespace cubeshot
{
bool validWeapon(WeaponKind kind)
{
    return static_cast<unsigned>(kind) <= 6;
}

const WeaponDefinition& weaponDefinition(WeaponKind kind)
{
    static const std::array<WeaponDefinition, 7> definitions{
        {{U"Пистолет", 20, 1, .25f, 800, 3, 0, 240, 36, 0, false},
         {U"Дробовик", 18, 6, .9f, 850, .38f, 12, 240, 44, 0, false},
         {U"Автомат", 15, 1, .12f, 1100, 1.35f, 2.5f, 160, 48, 0, true},
         {U"SMG", 10, 1, .075f, 850, .6f, 7, 220, 32, 0, true},
         {U"Револьвер", 35, 1, .65f, 1250, 1.8f, .25f, 100, 40, 0, false},
         {U"Винтовка", 70, 1, 1.2f, 1500, 1.8f, .12f, 70, 54, 0, false},
         {U"Снайперская винтовка", 100, 1, 2.f, 1800, 2.f, .04f, 40, 60, .3f, false}}};
    return definitions[validWeapon(kind) ? static_cast<unsigned>(kind) : 0];
}

void Weapon::equip(WeaponKind value)
{
    kind = validWeapon(value) ? value : WeaponKind::Pistol;
    const auto& d = weaponDefinition(kind);
    damage = d.damage;
    bulletSpeed = d.speed;
    shotInterval = d.interval;
    cooldown = preparation = 0;
    charging = false;
}

float Weapon::barrel() const
{
    return weaponDefinition(kind).barrel;
}

bool Weapon::automatic() const
{
    return weaponDefinition(kind).automatic;
}

bool Weapon::tryFire()
{
    if (cooldown > 0)
        return false;
    cooldown = shotInterval;
    ++shots;
    return true;
}

void Weapon::update(float dt)
{
    if (dt > 0)
    {
        cooldown = std::max(0.f, cooldown - dt);
        if (charging)
            preparation = std::max(0.f, preparation - dt);
    }
}
} // namespace cubeshot
