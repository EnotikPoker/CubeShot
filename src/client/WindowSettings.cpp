#include "client/WindowSettings.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace cubeshot::client
{
bool SdlWindowSettings::apply(const DisplaySettings& settings, std::string& error)
{
    if (!validDisplay(settings))
    {
        error = "Invalid display settings";
        return false;
    }
    if (!settings.fullscreen)
    {
        if (!SDL_SetWindowFullscreen(window_, false) ||
            !SDL_SetWindowSize(window_, settings.size.width, settings.size.height))
        {
            error = SDL_GetError();
            return false;
        }
    }
    else if (!SDL_SetWindowFullscreenMode(window_, nullptr) || !SDL_SetWindowFullscreen(window_, true))
    {
        error = SDL_GetError();
        return false;
    }
    if (!SDL_SetRenderVSync(renderer_, settings.vsync ? 1 : 0))
    {
        error = SDL_GetError();
        return false;
    }
    error.clear();
    return true;
}

bool SdlWindowSettings::settled(const DisplaySettings& settings) const
{
    const bool fullscreen = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    if (fullscreen != settings.fullscreen)
        return false;
    if (fullscreen)
        return true;
    int width = 0, height = 0;
    return SDL_GetWindowSize(window_, &width, &height) && width == settings.size.width &&
           height == settings.size.height;
}

std::vector<WindowSize> SdlWindowSettings::availableSizes() const
{
    SDL_Rect bounds{};
    int top = 0, left = 0, bottom = 0, right = 0;
    SDL_GetWindowBordersSize(window_, &top, &left, &bottom, &right);
    std::vector<WindowSize> result;
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_), &bounds))
        for (const auto size : windowSizes)
            if (size.width + left + right <= bounds.w && size.height + top + bottom <= bounds.h)
                result.push_back(size);
    if (result.empty())
        result.push_back(windowSizes.front());
    return result;
}

bool FramePacer::due(std::uint64_t now, int limit) const
{
    return limit == 0 || now >= next_;
}

void FramePacer::presented(std::uint64_t now, int limit)
{
    next_ = limit > 0 ? now + 1000000000ull / static_cast<unsigned>(limit) : now;
}

unsigned FramePacer::waitMilliseconds(std::uint64_t now, int limit) const
{
    if (limit == 0 || now >= next_)
        return 0;
    return static_cast<unsigned>(std::min<std::uint64_t>(8, (next_ - now) / 1000000));
}
} // namespace cubeshot::client
