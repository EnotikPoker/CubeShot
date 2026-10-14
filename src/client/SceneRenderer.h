#pragma once
#include "game/Types.h"

struct SDL_Renderer;

namespace cubeshot {
class World;
struct WorldSnapshot;
}

namespace cubeshot::client {

bool renderSnapshot(SDL_Renderer* renderer, const WorldSnapshot& snapshot, PlayerId own);

bool renderScene(SDL_Renderer* renderer, const World& world, bool present = true);

} // namespace cubeshot::client
