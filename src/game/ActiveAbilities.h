#pragma once
#include "game/Types.h"
#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace cubeshot
{
enum class ActiveKind : unsigned
{
    None,
    Dash,
    Blink,
    Barrier,
    Heal,
    Harpoon,
    Slam,
    Wave,
    Bomb,
    Parry,
    IceGrenade,
    Mine,
    Screen,
    Overdrive,
    Cleanse,
    Count
};
inline constexpr unsigned activeCount = 14;

struct ActiveDefinition
{
    std::u32string_view name, description;
    float cooldown;
};

struct ActiveSlot
{
    ActiveKind kind = ActiveKind::None;
    float cooldown = 0;
};

using ActiveLoadout = std::array<ActiveKind, 2>;
using ActiveSlots = std::array<ActiveSlot, 2>;
bool validActive(ActiveKind kind, bool empty = true);
bool validActiveLoadout(const ActiveLoadout& loadout);
const ActiveDefinition& activeDefinition(ActiveKind kind);
struct Player;
struct Arena;
std::optional<Vec2> blinkDestination(const Player& player, Vec2 direction, const Arena& arena,
                                     const std::vector<Player>& others);
bool activateLocal(Player& player, unsigned slot, const Arena& arena, const std::vector<Player>& others);
} // namespace cubeshot
