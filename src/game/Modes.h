#pragma once
#include "game/Weapon.h"
#include <array>
#include <string_view>
#include <vector>

namespace cubeshot
{
enum class GameMode : unsigned
{
    Duel,
    ArmsRace,
    Control,
    Chaos,
    Count
};
enum class ChaosKind : unsigned
{
    None,
    LowGravity,
    FastProjectiles,
    FastReload,
    HeavyRecoil,
    FragileCover,
    FastMovement,
    Count
};

struct SimulationModifiers
{
    float gravity = 1, projectiles = 1, reload = 1, recoil = 1, terrain = 1, movement = 1;
};

std::u32string_view modeName(GameMode mode);
std::u32string_view chaosName(ChaosKind kind);
SimulationModifiers chaosModifiers(ChaosKind kind);
std::vector<WeaponKind> armsChain(unsigned count);
} // namespace cubeshot
