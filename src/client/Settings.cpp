#include "client/Settings.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace cubeshot::client
{
Settings::Settings()
{
    bindings[0] = {{{BindingDevice::Keyboard, SDL_SCANCODE_A}, {BindingDevice::Keyboard, SDL_SCANCODE_LEFT}}};
    bindings[1] = {
        {{BindingDevice::Keyboard, SDL_SCANCODE_D}, {BindingDevice::Keyboard, SDL_SCANCODE_RIGHT}}};
    bindings[2][0] = {BindingDevice::Keyboard, SDL_SCANCODE_SPACE};
    bindings[3][0] = {BindingDevice::Mouse, SDL_BUTTON_LEFT};
    bindings[4][0] = {BindingDevice::Keyboard, SDL_SCANCODE_R};
}

bool validBinding(InputBinding binding)
{
    if (binding.device == BindingDevice::None)
        return binding.code == 0;
    if (binding.device == BindingDevice::Mouse)
        return binding.code >= 1 && binding.code <= 5;
    return binding.device == BindingDevice::Keyboard && binding.code > SDL_SCANCODE_UNKNOWN &&
           binding.code < SDL_SCANCODE_COUNT && binding.code != SDL_SCANCODE_ESCAPE &&
           *SDL_GetScancodeName(static_cast<SDL_Scancode>(binding.code)) != '\0';
}

bool validBindings(const InputBindings& bindings)
{
    for (std::size_t action = 0; action < bindings.size(); ++action)
    {
        if (action != static_cast<std::size_t>(InputAction::Restart) &&
            bindings[action][0].device == BindingDevice::None &&
            bindings[action][1].device == BindingDevice::None)
            return false;
        for (unsigned slot = 0; slot < 2; ++slot)
        {
            const auto binding = bindings[action][slot];
            if (!validBinding(binding))
                return false;
            if (binding.device == BindingDevice::None)
                continue;
            for (std::size_t a = 0; a <= action; ++a)
                for (unsigned s = 0; s < 2; ++s)
                    if ((a < action || s < slot) && bindings[a][s] == binding)
                        return false;
        }
    }
    return true;
}

bool validDisplay(const DisplaySettings& display)
{
    return std::find(windowSizes.begin(), windowSizes.end(), display.size) != windowSizes.end() &&
           std::find(frameLimits.begin(), frameLimits.end(), display.fps) != frameLimits.end();
}

std::u32string_view actionName(InputAction action)
{
    static constexpr std::array<std::u32string_view, 5> names{U"Влево", U"Вправо", U"Прыжок", U"Выстрел",
                                                              U"Перезапуск теста"};
    return names.at(static_cast<std::size_t>(action));
}

std::u32string bindingName(InputBinding binding)
{
    if (binding.device == BindingDevice::None)
        return U"Пусто";
    if (binding.device == BindingDevice::Mouse)
    {
        static constexpr std::array<std::u32string_view, 5> names{U"ЛКМ", U"СКМ", U"ПКМ", U"Мышь 4",
                                                                  U"Мышь 5"};
        return std::u32string(names.at(binding.code - 1));
    }
    const std::string name = SDL_GetScancodeName(static_cast<SDL_Scancode>(binding.code));
    // SDL's physical scancode names are ASCII, independent of the keyboard layout.
    return {name.begin(), name.end()};
}

std::u32string assignBinding(InputBindings& bindings, InputAction action, unsigned slot, InputBinding value)
{
    if (!validBinding(value) || slot >= 2)
        return U"Недопустимая кнопка";
    auto& target = bindings.at(static_cast<std::size_t>(action)).at(slot);
    if (target == value)
        return {};
    if (value.device != BindingDevice::None)
        for (std::size_t a = 0; a < bindings.size(); ++a)
            for (const auto& bound : bindings[a])
                if (bound == value)
                    return U"Занято: " + std::u32string(actionName(static_cast<InputAction>(a)));
    const auto before = target;
    target = value;
    if (!validBindings(bindings))
    {
        target = before;
        return U"Оставьте хотя бы одну кнопку";
    }
    return {};
}
} // namespace cubeshot::client
