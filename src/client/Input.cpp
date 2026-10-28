#include "client/Input.h"
#include "client/WorldView.h"
#include "game/Player.h"

#include <SDL3/SDL.h>
#include <cmath>

namespace cubeshot::client
{

bool actionHeld(InputAction action, const InputBindings& bindings, const bool* keys, int count,
                std::uint32_t mouse)
{
    for (const auto binding : bindings[static_cast<std::size_t>(action)])
    {
        if (binding.device == BindingDevice::Keyboard && binding.code > 0 && binding.code < count &&
            keys[binding.code])
            return true;
        if (binding.device == BindingDevice::Mouse && binding.code >= 1 && binding.code <= 5 &&
            (mouse & SDL_BUTTON_MASK(binding.code)))
            return true;
    }
    return false;
}

InputBinding eventBinding(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
        return {BindingDevice::Keyboard, event.key.scancode};
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        return {BindingDevice::Mouse, event.button.button};
    return {};
}

PlayerInput readPlayerInput(SDL_Window* window, SDL_Renderer* renderer, const Player& player)
{
    return readPlayerInput(window, renderer, player, Settings{}.bindings);
}

PlayerInput readPlayerInput(SDL_Window* window, SDL_Renderer* renderer, const Player& player,
                            const InputBindings& bindings, Rect arenaBounds)
{
    PlayerInput input;
    input.aimDirection = player.aimDirection;
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) == 0)
    {
        return input;
    }

    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouseX, &mouseY);
    float aimX = 0.0f;
    float aimY = 0.0f;
    if (SDL_RenderCoordinatesFromWindow(renderer, mouseX, mouseY, &aimX, &aimY))
    {
        const Vec2 origin = player.weaponOrigin();
        const Vec2 world = WorldView(arenaBounds).toWorld({aimX, aimY});
        const Vec2 aim{world.x - origin.x, world.y - origin.y};
        if (std::hypot(aim.x, aim.y) > 0.0001f)
            input.aimDirection = aim;
    }
    const bool left = actionHeld(InputAction::Left, bindings, keys, count, buttons);
    const bool right = actionHeld(InputAction::Right, bindings, keys, count, buttons);
    input.moveDirection = static_cast<float>(right) - static_cast<float>(left);
    input.jump = actionHeld(InputAction::Jump, bindings, keys, count, buttons);
    input.shoot = actionHeld(InputAction::Shoot, bindings, keys, count, buttons);
    return input;
}

void InputEventState::synchronize()
{
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    keys_.fill(false);
    for (int i = 0; i < count && i < SDL_SCANCODE_COUNT; ++i)
        keys_[i] = keys[i];
    mouse_ = SDL_GetMouseState(nullptr, nullptr);
}

std::array<bool, static_cast<std::size_t>(InputAction::Count)>
InputEventState::press(const SDL_Event& event, const InputBindings& bindings)
{
    std::array<bool, static_cast<std::size_t>(InputAction::Count)> result{};
    const auto binding = eventBinding(event);
    if (binding.device == BindingDevice::None)
        return result;
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat)
        return result;
    const bool down = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    for (std::size_t a = 0; a < result.size(); ++a)
        result[a] =
            actionHeld(static_cast<InputAction>(a), bindings, keys_.data(), SDL_SCANCODE_COUNT, mouse_);
    if (binding.device == BindingDevice::Keyboard && binding.code > 0 && binding.code < SDL_SCANCODE_COUNT)
        keys_[binding.code] = down;
    if (binding.device == BindingDevice::Mouse && binding.code >= 1 && binding.code <= 5)
    {
        if (down)
            mouse_ |= SDL_BUTTON_MASK(binding.code);
        else
            mouse_ &= ~SDL_BUTTON_MASK(binding.code);
    }
    for (std::size_t a = 0; a < result.size(); ++a)
        result[a] = !result[a] && actionHeld(static_cast<InputAction>(a), bindings, keys_.data(),
                                             SDL_SCANCODE_COUNT, mouse_);
    return result;
}

void GameplayInputGate::reset(const PlayerInput& heldInput)
{
    blockJump_ = heldInput.jump;
    blockShoot_ = heldInput.shoot;
}

PlayerInput GameplayInputGate::filter(PlayerInput input)
{
    if (!input.jump)
        blockJump_ = false;
    if (!input.shoot)
        blockShoot_ = false;
    input.jump = input.jump && !blockJump_;
    input.shoot = input.shoot && !blockShoot_;
    input.triggerHeld = input.shoot;
    return input;
}

} // namespace cubeshot::client
