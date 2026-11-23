#include "game/AttackStats.h"
#include "game/Bullet.h"
#include <algorithm>

namespace cubeshot
{
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
        case AbilityKind::WideSwing:
        case AbilityKind::Lunge:
        case AbilityKind::Bleeding:
        case AbilityKind::Combo:
            return melee;
        case AbilityKind::ReturnStrike:
        case AbilityKind::SecondCircuit:
            return boom;
        case AbilityKind::Magazine:
        case AbilityKind::FastReload:
            return weaponDefinition(w).capacity > 0;
        case AbilityKind::LargeBullets:
            return ballistic || explosiveWeapon(w) || boom || w == WeaponKind::Flamethrower;
        case AbilityKind::ProjectileSpeed:
            return ballistic || explosiveWeapon(w) || boom;
        case AbilityKind::Accuracy:
            return weaponDefinition(w).spread > 0;
        case AbilityKind::TwinShot:
        case AbilityKind::Ricochet:
        case AbilityKind::Penetration:
            return ballistic || boom;
        case AbilityKind::BlastRadius:
            return explosiveWeapon(w) ||
                   std::any_of(
                       slots.begin(), slots.end(), [](auto k)
                       { return k == ActiveKind::Bomb || k == ActiveKind::Slam || k == ActiveKind::Wave; });
        case AbilityKind::Range:
            return !explosiveWeapon(w);
        case AbilityKind::Recoil:
            return weaponDefinition(w).recoil > 0;
        default:
            return validAbility(a);
    }
}

ExplosionStats explosionStats(const Bullet& b)
{
    if (b.active == ActiveKind::Bomb)
        return {150 * b.blastScale, 60, 96 * b.blastScale, 100};
    if (b.weapon == WeaponKind::GrenadeLauncher)
        return {180 * b.blastScale, 90, 96 * b.blastScale, 140};
    return {240 * b.blastScale, 120, 144 * b.blastScale, 200};
}
} // namespace cubeshot
