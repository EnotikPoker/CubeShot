#pragma once

#include "game/PlayerInput.h"

struct SDL_Window;
struct SDL_Renderer;

namespace cubeshot { struct Player; }

namespace cubeshot::client {

PlayerInput readPlayerInput(SDL_Window* window, SDL_Renderer* renderer, const Player& player);

// Кнопки, удерживаемые при входе в сцену, требуют отпускания перед действием.
class GameplayInputGate {
public:
    void reset(const PlayerInput& heldInput);
    PlayerInput filter(PlayerInput input);
private:
    bool blockJump_ = false;
    bool blockShoot_ = false;
};

} // namespace cubeshot::client
