#include "game/Weapon.h"

#include <algorithm>

namespace cubeshot {

void Weapon::update(float deltaSeconds)
{
    if (deltaSeconds > 0.0f) {
        cooldown = std::max(0.0f, cooldown - deltaSeconds);
    }
}

} // namespace cubeshot
