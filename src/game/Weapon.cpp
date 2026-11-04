#include "game/Weapon.h"
#include <algorithm>
#include <array>
#include <cmath>

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
        {{U"Пистолет", 20, 1, .25f, 800, 8, 0, 240, 36, 0, false, 12, 1.25f, 1100, 30},
         {U"Дробовик", 18, 6, .9f, 850, 8, 12, 240, 44, 0, false, 6, .5f, 260, 450},
         {U"Автомат", 12, 1, .18f, 900, 8, 4, 50, 48, 0, true, 30, 1.8f, 750, 20},
         {U"SMG", 10, 1, .075f, 850, 8, 7, 220, 32, 0, true, 35, 1.6f, 450, 14},
         {U"Револьвер", 35, 1, .65f, 1250, 8, .25f, 100, 40, 0, false, 6, 2.2f, 1600, 90},
         {U"Винтовка", 70, 1, 1.2f, 1500, 8, .12f, 70, 54, 0, false, 5, 2.2f, 2000, 130},
         {U"Снайперская винтовка", 100, 1, 2, 1800, 8, .04f, 40, 60, .3f, false, 3, 2.8f, 3000, 200},
         {U"Миниган", 5, 1, .04f, 950, 8, 8, 160, 54, 0, true, 300, 4, 550, 90},
         {U"РПГ", 100, 1, 2.2f, 650, 5, 0, 25, 56, 0, false, 1, 2.8f, 0, 120},
         {U"Огнемёт", 5, 1, .075f, 600, .7f, 9, -35, 40, 0, true, 100, 3, 350, 0},
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
    ammo = capacity();
    cancelReload();
}

float Weapon::barrel() const
{
    return weaponDefinition(kind).barrel * bodyScale;
}

bool Weapon::automatic() const
{
    return weaponDefinition(kind).automatic;
}

int Weapon::capacity() const
{
    const int base = weaponDefinition(kind).capacity;
    return base ? base + static_cast<int>(std::ceil(base * .25f)) * static_cast<int>(magazineLevel) : 0;
}

float Weapon::reloadDuration() const
{
    return weaponDefinition(kind).reloadTime / (1 + .25f * reloadLevel);
}

float Weapon::reloadSetup() const
{
    return kind == WeaponKind::Shotgun ? .35f / (1 + .25f * reloadLevel) : 0;
}

bool Weapon::startReload()
{
    const auto& d = weaponDefinition(kind);
    if (!d.capacity || ammo >= capacity() || reloading)
        return false;
    reloading = true;
    reloadRemaining = reloadDuration() + reloadSetup();
    charging = driven = false;
    preparation = 0;
    return true;
}

void Weapon::cancelReload()
{
    reloading = false;
    reloadRemaining = 0;
}

bool Weapon::tryFire()
{
    if (cooldown > 0)
        return false;
    if (reloading)
    {
        if (kind != WeaponKind::Shotgun || ammo <= 0)
            return false;
        cancelReload();
    }
    const auto& d = weaponDefinition(kind);
    if (d.capacity && ammo <= 0)
    {
        startReload();
        return false;
    }
    if (kind == WeaponKind::Minigun)
    {
        if (overheated || spin < .15f)
            return false;
        heat = std::min(1.f, heat + .004f);
        overheated = heat >= .99999f;
        cooldown = .14f + (.04f - .14f) * spin;
    }
    else
        cooldown = shotInterval;
    ++shots;
    if (d.capacity && --ammo == 0)
        startReload();
    return true;
}

void Weapon::update(float dt)
{
    if (!(dt > 0))
        return;
    if (reloading)
    {
        reloadRemaining -= dt;
        const auto& d = weaponDefinition(kind);
        while (reloading && reloadRemaining <= 0)
        {
            if (kind == WeaponKind::Shotgun && ++ammo < capacity())
                reloadRemaining += reloadDuration();
            else
            {
                ammo = capacity();
                cancelReload();
            }
        }
    }
    if (kind == WeaponKind::Minigun)
    {
        spin = std::clamp(spin + dt * (driven && !overheated && !reloading ? 1.f / 1.2f : -2.5f), 0.f, 1.f);
        if (!driven || overheated || reloading)
            heat = std::max(0.f, heat - dt * .3f);
        if (overheated && heat <= .25f)
            overheated = false;
    }
    cooldown = std::max(0.f, cooldown - dt);
    if (charging)
        preparation = std::max(0.f, preparation - dt);
}
} // namespace cubeshot
