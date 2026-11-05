#pragma once
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
    float scale() const;
    float speed() const;
    float jump() const;
    float reload() const;
    float recoil() const;
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
