#pragma once

#include "game/PlayerInput.h"

struct SDL_Window;

namespace cubeshot::client {

PlayerInput readPlayerInput(SDL_Window* window);

} // namespace cubeshot::client
