#pragma once

struct SDL_Renderer;

namespace cubeshot {
class World;
}

namespace cubeshot::client {

bool renderScene(SDL_Renderer* renderer, const World& world);

} // namespace cubeshot::client
