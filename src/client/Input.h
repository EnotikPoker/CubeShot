#pragma once
#include "client/Settings.h"
#include "game/PlayerInput.h"
#include <SDL3/SDL_scancode.h>
#include <cstdint>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;

namespace cubeshot
{
struct Player;
}

namespace cubeshot::client
{
bool actionHeld(InputAction action, const InputBindings& bindings, const bool* keys, int count,
                std::uint32_t mouse);
InputBinding eventBinding(const SDL_Event& event);
PlayerInput readPlayerInput(SDL_Window* window, SDL_Renderer* renderer, const Player& player,
                            const InputBindings& bindings, Rect arenaBounds = {{0, 0}, {960, 540}});
// Compatibility overload for existing callers; production uses the current settings.
PlayerInput readPlayerInput(SDL_Window* window, SDL_Renderer* renderer, const Player& player);

class InputEventState
{
public:
    void synchronize();
    std::array<bool, static_cast<std::size_t>(InputAction::Count)> press(const SDL_Event& event,
                                                                         const InputBindings& bindings);

private:
    std::array<bool, SDL_SCANCODE_COUNT> keys_{};
    std::uint32_t mouse_ = 0;
};

class GameplayInputGate
{
public:
    void reset(const PlayerInput& heldInput);
    PlayerInput filter(PlayerInput input);

private:
    bool blockJump_ = false, blockShoot_ = false;
};
} // namespace cubeshot::client
