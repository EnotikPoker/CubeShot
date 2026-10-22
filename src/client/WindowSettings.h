#pragma once
#include "client/Settings.h"
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;

namespace cubeshot::client
{
class WindowSettings
{
public:
    virtual ~WindowSettings() = default;
    virtual bool apply(const DisplaySettings& settings, std::string& error) = 0;
    virtual bool settled(const DisplaySettings& settings) const = 0;
    virtual std::vector<WindowSize> availableSizes() const = 0;
};

class SdlWindowSettings final : public WindowSettings
{
public:
    SdlWindowSettings(SDL_Window* window, SDL_Renderer* renderer) : window_(window), renderer_(renderer) {}

    bool apply(const DisplaySettings& settings, std::string& error) override;
    bool settled(const DisplaySettings& settings) const override;
    std::vector<WindowSize> availableSizes() const override;

private:
    SDL_Window* window_;
    SDL_Renderer* renderer_;
};

// Rendering deadline only: the caller continues input/network/simulation while waiting.
class FramePacer
{
public:
    bool due(std::uint64_t now, int limit) const;
    void presented(std::uint64_t now, int limit);
    unsigned waitMilliseconds(std::uint64_t now, int limit) const;

private:
    std::uint64_t next_ = 0;
};
} // namespace cubeshot::client
