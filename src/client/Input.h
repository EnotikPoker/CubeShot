#pragma once

#include "game/PlayerInput.h"

struct SDL_Window;
struct SDL_Renderer;

namespace cubeshot { struct Player; }

namespace cubeshot::client {

PlayerInput readPlayerInput(SDL_Window* window, SDL_Renderer* renderer, const Player& player);

} // namespace cubeshot::client
