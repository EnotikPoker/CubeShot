#include "game/AttackQueries.h"
#include "game/World.h"

namespace cubeshot
{
void World::electric(const Bullet& attack)
{
    auto contacts = electricContacts(attack, arena_, players_);
    unsigned hop = 0;
    for (const auto& contact : contacts)
    {
        auto shot = attack;
        shot.damage = contact.damage;
        emit(EventKind::ElectricArc, shot, contact.to, contact.target);
        events_.back().local = contact.from;
        events_.back().surfaceRevision = hop++;
        if (contact.target)
            for (auto& player : players_)
                if (player.id == contact.target)
                    damage(player, shot, contact.to, shot.damage);
        if (contact.terrain)
            arena_.damageTerrain(contact.to, 6, static_cast<int>(shot.damage * shot.terrainPower));
    }
}
} // namespace cubeshot
