#pragma once
#include "game/ActiveAbilities.h"
#include "game/Weapon.h"
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cubeshot
{
enum class AbilityKind : std::uint32_t
{
    SmallBody,
    Speed,
    Jump,
    Health,
    LargeBullets,
    Armor,
    FastReload,
    Magazine,
    Incendiary,
    Jetpack,
    Recoil,
    Power,
    FireRate,
    ProjectileSpeed,
    Range,
    Accuracy,
    TwinShot,
    Ricochet,
    Penetration,
    Demolition,
    BlastRadius,
    Vampirism,
    Regeneration,
    ToughSkin,
    FireResistance,
    Thorns,
    Adrenaline,
    DoubleJump,
    FeatherFall,
    Stability,
    SecondChance,
    WideSwing,
    Lunge,
    Bleeding,
    Combo,
    ReturnStrike,
    SecondCircuit,
    Count,
    None = Count
};
inline constexpr unsigned abilityCount = static_cast<unsigned>(AbilityKind::Count);
// Wire/overflow guard, far beyond an ordinary match; repeatable upgrades have no gameplay cap.
inline constexpr unsigned maximumAbilityStacks = 10000;

struct AbilityDefinition
{
    std::u32string_view name, description;
    unsigned maximum;
};

bool validAbility(AbilityKind kind);
const AbilityDefinition& abilityDefinition(AbilityKind kind);

struct Abilities
{
    std::array<unsigned, abilityCount> levels{};
    unsigned level(AbilityKind kind) const;
    bool canAdd(AbilityKind kind) const;
    bool add(AbilityKind kind);
    bool valid() const;
    std::vector<AbilityKind> available() const;
    std::vector<AbilityKind> available(WeaponKind weapon, const ActiveLoadout& active = {}) const;
    float scale() const;
    float speed() const;
    float jump() const;
    float reload() const;
    float recoil() const;
    float power() const;
    float fireRate() const;
    float projectileSpeed() const;
    float range() const;
    float accuracy() const;
    float demolition() const;
    float blastRadius() const;
    int health() const;
    unsigned shields() const;

    bool operator==(const Abilities& other) const
    {
        return levels == other.levels;
    }

    bool operator!=(const Abilities& other) const
    {
        return !(*this == other);
    }
};
} // namespace cubeshot
