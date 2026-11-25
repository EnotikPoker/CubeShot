#pragma once
#include "game/Bullet.h"
#include <optional>
#include <vector>

namespace cubeshot
{
struct Player;
struct Arena;

struct MeleeContact
{
    Vec2 point;
    float distance;
};

std::optional<MeleeContact> meleeContact(Vec2 origin, Vec2 direction, float reach, const Player& target,
                                         const Arena& arena, float halfAngle = .7953988f);
int healthDamage(int amount, float power, const Player& target, WeaponKind weapon, bool periodic = false);

struct ElectricContact
{
    Vec2 from, to;
    PlayerId target = 0;
    int damage = 0;
    bool terrain = false;
};

std::vector<ElectricContact> electricContacts(const Bullet& attack, const Arena& arena,
                                              const std::vector<Player>& players);
} // namespace cubeshot
