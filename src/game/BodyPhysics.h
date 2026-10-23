#pragma once
#include "game/Arena.h"
#include "game/Player.h"
#include <vector>

namespace cubeshot
{
// Shared by authority and prediction. Call at fixed steps <= 1/120 second.
void simulateBodies(std::vector<Player>& players, const Arena& arena, float dt);
} // namespace cubeshot
