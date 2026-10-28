#include "game/Weapon.h"
#include <algorithm>
#include <array>

namespace cubeshot
{
bool validWeapon(WeaponKind kind)
{
    return static_cast<unsigned>(kind) < weaponCount;
}

bool meleeWeapon(WeaponKind kind)
{
    return kind == WeaponKind::Bat || kind == WeaponKind::Katana || kind == WeaponKind::Knife;
}

bool incendiaryWeapon(WeaponKind kind)
{
    return kind == WeaponKind::Flamethrower || kind == WeaponKind::FireBoomerang;
}

const WeaponDefinition& weaponDefinition(WeaponKind kind)
{
    static const std::array<WeaponDefinition, weaponCount> definitions{
        {{U"Пистолет", 20, 1, .25f, 800, 3, 0, 240, 36, 0, false},
         {U"Дробовик", 18, 6, .9f, 850, .38f, 12, 240, 44, 0, false},
         {U"Автомат", 12, 1, .18f, 900, .65f, 4.f, 160, 48, 0, true},
         {U"SMG", 10, 1, .075f, 850, .6f, 7, 220, 32, 0, true},
         {U"Револьвер", 35, 1, .65f, 1250, 1.8f, .25f, 100, 40, 0, false},
         {U"Винтовка", 70, 1, 1.2f, 1500, 1.8f, .12f, 70, 54, 0, false},
         {U"Снайперская винтовка", 100, 1, 2.f, 1800, 2.f, .04f, 40, 60, .3f, false},
         {U"Миниган", 5, 1, .04f, 950, .7f, 8, 160, 54, 0, true},
         {U"РПГ", 100, 1, 2.2f, 300, 4, 0, 25, 56, 0, false},
         {U"Огнемёт", 3, 1, .075f, 480, .42f, 9, -35, 40, 0, true},
         {U"Бита", 25, 1, .55f, 1000, .1f, 0, 0, 82, 0, false},
         {U"Катана", 30, 1, .32f, 1000, .1f, 0, 0, 90, 0, false},
         {U"Нож", 15, 1, .18f, 1000, .1f, 0, 0, 52, 0, false},
         {U"Огненный бумеранг", 20, 1, 1.8f, 560, 2.4f, 0, 0, 30, 0, false}}};
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
    charging = driven = overheated = false;
    spin = heat = 0;
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
    if (kind == WeaponKind::Minigun)
    {
        if (overheated || spin < .15f)
            return false;
        heat = std::min(1.f, heat + .022f);
        overheated = heat >= 1;
        cooldown = .14f + (.04f - .14f) * spin;
    }
    else
        cooldown = shotInterval;
    ++shots;
    return true;
}

void Weapon::update(float dt)
{
    if (dt > 0)
    {
        if (kind == WeaponKind::Minigun)
        {
            spin = std::clamp(spin + dt * (driven && !overheated ? 1.f / 1.2f : -2.5f), 0.f, 1.f);
            if (!driven || overheated)
                heat = std::max(0.f, heat - dt * .3f);
            if (overheated && heat <= .25f)
                overheated = false;
        }
        cooldown = std::max(0.f, cooldown - dt);
        if (charging)
            preparation = std::max(0.f, preparation - dt);
    }
}
} // namespace cubeshot
