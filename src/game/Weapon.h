#pragma once
#include <cstdint>
#include <string_view>

namespace cubeshot
{
enum class WeaponKind : std::uint32_t
{
    Pistol,
    Shotgun,
    Automatic,
    Smg,
    Revolver,
    Rifle,
    Sniper,
    Minigun,
    Rpg,
    Flamethrower,
    Bat,
    Katana,
    Knife,
    FireBoomerang
};

struct WeaponDefinition
{
    std::u32string_view name;
    int damage, pellets;
    float interval, speed, lifetime, spread, gravity, barrel, preparation;
    bool automatic;
};

inline constexpr unsigned weaponCount = 14;
bool meleeWeapon(WeaponKind kind);
bool incendiaryWeapon(WeaponKind kind);
bool validWeapon(WeaponKind kind);
const WeaponDefinition& weaponDefinition(WeaponKind kind);

struct Weapon
{
    static constexpr float barrelLength = 36;
    WeaponKind kind = WeaponKind::Pistol;
    int damage = 20;
    float bulletSpeed = 800, shotInterval = .25f, cooldown = 0;
    bool charging = false;
    bool driven = false, overheated = false;
    float spin = 0, heat = 0;
    float preparation = 0;
    std::uint32_t shots = 0;
    void equip(WeaponKind value);
    float barrel() const;
    bool automatic() const;
    void update(float dt);
    bool tryFire();
};
} // namespace cubeshot
