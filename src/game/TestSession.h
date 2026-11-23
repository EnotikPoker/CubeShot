#pragma once
#include "game/Abilities.h"
#include "game/Weapon.h"
#include <array>

namespace cubeshot
{
struct TestConfiguration
{
    std::array<Abilities, 4> abilities{};
    std::array<unsigned, 4> healthPercent{{100, 100, 100, 100}};
    std::array<WeaponKind, 4> weapons{};
    std::array<ActiveLoadout, 4> actives{};
};

enum class TestOperation
{
    Build,
    AllBuilds,
    Health,
    Heal,
    Respawn,
    EmptyShields,
    RefillShields,
    EmptyFuel,
    RefillFuel,
    EmptyAmmo,
    RefillAmmo,
    Reload,
    FinishReload,
    Overheat,
    Cool,
    Extinguish,
    RestoreMap,
    Clear,
    ReadyActives,
    UseQ,
    UseE
};

struct TestEdit
{
    TestOperation operation = TestOperation::Build;
    unsigned actor = 0;
};
} // namespace cubeshot
