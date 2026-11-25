#pragma once
#include "game/Abilities.h"
#include "game/ActiveAbilities.h"
#include "game/Weapon.h"

namespace cubeshot
{
struct AttackStats
{
    float reach = 0, halfAngle = .7953988f, boomOut = 560, boomReturn = 700, boomRange = 308, returnPower = 1;
    unsigned circuits = 0;
};

AttackStats attackStats(WeaponKind weapon, const Abilities& abilities);
bool passiveApplies(AbilityKind kind, WeaponKind weapon, const ActiveLoadout& active = {});
struct Bullet;

struct ExplosionStats
{
    float radius;
    int damage;
    float terrainRadius;
    int terrainDamage;
};

ExplosionStats explosionStats(const Bullet& bullet);
} // namespace cubeshot
