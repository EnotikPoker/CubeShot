#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace cubeshot::client
{
enum class InputAction
{
    Left,
    Right,
    Jump,
    Shoot,
    Restart,
    Count
};
enum class BindingDevice
{
    None,
    Keyboard,
    Mouse
};

struct InputBinding
{
    BindingDevice device = BindingDevice::None;
    int code = 0;

    bool operator==(const InputBinding& other) const
    {
        return device == other.device && code == other.code;
    }

    bool operator!=(const InputBinding& other) const
    {
        return !(*this == other);
    }
};

using InputBindings = std::array<std::array<InputBinding, 2>, static_cast<std::size_t>(InputAction::Count)>;

struct WindowSize
{
    int width = 960, height = 540;

    bool operator==(const WindowSize& other) const
    {
        return width == other.width && height == other.height;
    }

    bool operator!=(const WindowSize& other) const
    {
        return !(*this == other);
    }
};

inline constexpr std::array<WindowSize, 4> windowSizes{{{960, 540}, {1280, 720}, {1600, 900}, {1920, 1080}}};
inline constexpr std::array<int, 7> frameLimits{{30, 60, 120, 144, 165, 240, 0}};

struct DisplaySettings
{
    bool fullscreen = false;
    WindowSize size;
    bool vsync = false;
    int fps = 120;

    bool operator==(const DisplaySettings& other) const
    {
        return fullscreen == other.fullscreen && size == other.size && vsync == other.vsync &&
               fps == other.fps;
    }

    bool operator!=(const DisplaySettings& other) const
    {
        return !(*this == other);
    }
};

struct Settings
{
    DisplaySettings display;
    InputBindings bindings;
    Settings();
};

bool validBinding(InputBinding binding);
bool validBindings(const InputBindings& bindings);
bool validDisplay(const DisplaySettings& display);
std::u32string_view actionName(InputAction action);
std::u32string bindingName(InputBinding binding);
// Reject conflicts and deletion of the final required binding. Returns a UI error, or empty on success.
std::u32string assignBinding(InputBindings& bindings, InputAction action, unsigned slot, InputBinding value);
} // namespace cubeshot::client
