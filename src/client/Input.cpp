#include "client/Input.h"
#include "game/Player.h"

#include <SDL3/SDL.h>
#include <cmath>

namespace cubeshot::client {

PlayerInput readPlayerInput(SDL_Window* window, SDL_Renderer* renderer, const Player& player)
{
    PlayerInput input;
    input.aimDirection = player.aimDirection;
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) == 0) {
        return input;
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);
    const bool left = keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT];
    const bool right = keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT];
    input.moveDirection = static_cast<float>(right) - static_cast<float>(left);
    input.jump = keys[SDL_SCANCODE_SPACE];
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouseX, &mouseY);
    float aimX = 0.0f;
    float aimY = 0.0f;
    if (SDL_RenderCoordinatesFromWindow(renderer, mouseX, mouseY, &aimX, &aimY)) {
        const Vec2 origin = player.weaponOrigin();
        const Vec2 aim{aimX - origin.x, aimY - origin.y};
        if (std::hypot(aim.x, aim.y) > 0.0001f) input.aimDirection = aim;
    }
    input.shoot = (buttons & SDL_BUTTON_LMASK) != 0;
    return input;
}

void GameplayInputGate::reset(const PlayerInput& heldInput)
{
    blockJump_ = heldInput.jump;
    blockShoot_ = heldInput.shoot;
}

PlayerInput GameplayInputGate::filter(PlayerInput input)
{
    if (!input.jump) blockJump_ = false;
    if (!input.shoot) blockShoot_ = false;
    input.jump = input.jump && !blockJump_;
    input.shoot = input.shoot && !blockShoot_;
    return input;
}

} // namespace cubeshot::client
