#include "client/Input.h"

#include <SDL3/SDL.h>

namespace cubeshot::client {

PlayerInput readPlayerInput(SDL_Window* window)
{
    PlayerInput input;
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) == 0) {
        return input;
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    const bool right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
    input.moveDirection = static_cast<float>(right) - static_cast<float>(left);
    input.jump = keys[SDL_SCANCODE_SPACE];
    return input;
}

} // namespace cubeshot::client
