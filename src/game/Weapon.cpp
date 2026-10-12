#include "game/Weapon.h"

#include <algorithm>

namespace cubeshot {

bool Weapon::tryFire()
{
    if (cooldown > 0.0f) {
        return false;
    }
    cooldown = shotInterval;
    return true;
}

void Weapon::update(float deltaSeconds)
{
    if (deltaSeconds > 0.0f) {
        cooldown = std::max(0.0f, cooldown - deltaSeconds);
    }
}

} // namespace cubeshot
