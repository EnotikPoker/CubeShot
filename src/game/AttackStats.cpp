#include "game/AttackStats.h"
#include "game/Bullet.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
std::u32string abilityEffect(AbilityKind kind, const Abilities& a)
{
    auto n = a.level(kind);
    auto number = [](unsigned v)
    {
        auto s = std::to_string(v);
        return std::u32string(s.begin(), s.end());
    };
    std::u32string text(abilityDefinition(kind).description), total;
    switch (kind)
    {
        case AbilityKind::Speed:
        case AbilityKind::Jump:
            total = U"+" + number(30 * n) + U"%";
            break;
        case AbilityKind::Health:
            total = number(a.health()) + U" HP";
            break;
        case AbilityKind::Armor:
            total = number(a.shields()) + U" щитов";
            break;
        case AbilityKind::FastReload:
        case AbilityKind::Magazine:
        case AbilityKind::Range:
        case AbilityKind::ProjectileSpeed:
            total = U"+" + number(25 * n) + U"%";
            break;
        case AbilityKind::Power:
        case AbilityKind::AirManeuver:
            total = U"+" + number(15 * n) + U"%";
            break;
        case AbilityKind::FireRate:
            total = U"+" + number(12 * n) + U"%";
            break;
        case AbilityKind::Impulse:
            total = number(80 * n) + U" импульса";
            break;
        case AbilityKind::Demolition:
            total = U"+" + number(50 * n) + U"%";
            break;
        case AbilityKind::BlastRadius:
            total = U"+" + number(20 * n) + U"%";
            break;
        case AbilityKind::ToughSkin:
            total = U"−" + number(10 * n) + U"% урона";
            break;
        case AbilityKind::WideSwing:
            total = U"+" + number(30 * n) + U"°";
            break;
        case AbilityKind::Lunge:
            total = number(120 * n) + U" импульса";
            break;
        case AbilityKind::Regeneration:
            total = number(4 * n) + U" HP/с";
            break;
        case AbilityKind::ReturnStrike:
            total = U"+" + number(25 * n) + U"% на возврате";
            break;
        case AbilityKind::Transfer:
            total = number(n * 5 / 10) + U"," + number(n * 5 % 10) + U" с";
            break;
        case AbilityKind::Accuracy:
            total = number(unsigned(std::lround(a.accuracy() * 100))) + U"% базового разброса";
            break;
        default:
            break;
    }
    return total.empty() ? text : U"Итого: " + total + U" · " + text;
}

AttackStats attackStats(WeaponKind w, const Abilities& a)
{
    AttackStats s;
    s.reach = weaponDefinition(w).barrel * (meleeWeapon(w) ? a.range() : 1);
    s.halfAngle = std::min(1.5707963f, .7953988f + .2617994f * a.level(AbilityKind::WideSwing));
    s.boomOut = 560 * a.projectileSpeed();
    s.boomReturn = 700 * a.projectileSpeed();
    s.boomRange = 308 * a.range();
    s.returnPower = 1 + .25f * a.level(AbilityKind::ReturnStrike);
    s.circuits = a.level(AbilityKind::SecondCircuit);
    return s;
}

bool passiveApplies(AbilityKind a, WeaponKind w, const ActiveLoadout& slots)
{
    bool melee = meleeWeapon(w), boom = w == WeaponKind::FireBoomerang, ballistic = ballisticWeapon(w);
    switch (a)
    {
        case AbilityKind::Stance:
        case AbilityKind::Echo:
        case AbilityKind::WideSwing:
        case AbilityKind::Lunge:
        case AbilityKind::Bleeding:
        case AbilityKind::Combo:
            return melee;
        case AbilityKind::ReturnStrike:
        case AbilityKind::SecondCircuit:
            return boom;
        case AbilityKind::FreshMagazine:
            return weaponDefinition(w).capacity > 0 && w != WeaponKind::Beam;
        case AbilityKind::Economy:
        case AbilityKind::Magazine:
        case AbilityKind::FastReload:
            return weaponDefinition(w).capacity > 0;
        case AbilityKind::LargeBullets:
            return ballistic || explosiveWeapon(w) || boom || w == WeaponKind::Flamethrower ||
                   w == WeaponKind::StickyLauncher;
        case AbilityKind::ProjectileSpeed:
            return ballistic || explosiveWeapon(w) || boom || w == WeaponKind::StickyLauncher;
        case AbilityKind::Accuracy:
            return weaponDefinition(w).spread > 0;
        case AbilityKind::TwinShot:
        case AbilityKind::Ricochet:
        case AbilityKind::Penetration:
            return ballistic || boom;
        case AbilityKind::BlastRadius:
            return explosiveWeapon(w) || w == WeaponKind::StickyLauncher ||
                   std::any_of(slots.begin(), slots.end(),
                               [](auto k)
                               {
                                   return k == ActiveKind::Bomb || k == ActiveKind::Slam ||
                                          k == ActiveKind::Wave || k == ActiveKind::IceGrenade ||
                                          k == ActiveKind::Mine;
                               });
        case AbilityKind::Range:
            return !explosiveWeapon(w) && w != WeaponKind::StickyLauncher;
        case AbilityKind::Recoil:
            return weaponDefinition(w).recoil > 0;
        default:
            return validAbility(a);
    }
}

ExplosionStats explosionStats(const Bullet& b)
{
    if (b.blastDamage > 0)
        return {b.blastRadius * b.blastScale, b.blastDamage, b.terrainRadius * b.blastScale, b.terrainDamage};
    if (b.active == ActiveKind::Bomb)
        return {150 * b.blastScale, 60, 96 * b.blastScale, 100};
    if (b.weapon == WeaponKind::GrenadeLauncher)
        return {180 * b.blastScale, 90, 96 * b.blastScale, 140};
    return {240 * b.blastScale, 120, 144 * b.blastScale, 200};
}
} // namespace cubeshot
