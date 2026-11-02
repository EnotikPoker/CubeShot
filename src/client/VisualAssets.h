#pragma once
#include "game/Types.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>

namespace cubeshot::client
{
class TextRenderer;

class VisualAssets
{
public:
    explicit VisualAssets(SDL_Renderer* renderer);
    ~VisualAssets();
    VisualAssets(const VisualAssets&) = delete;
    VisualAssets& operator=(const VisualAssets&) = delete;
    static VisualAssets* get(SDL_Renderer* renderer);
    static std::filesystem::path directory();
    std::uint64_t identity = 0;
    SDL_Texture* effects = nullptr;
    SDL_Texture* materials = nullptr;
    std::unique_ptr<TextRenderer> text;

private:
    SDL_Renderer* renderer_;
};

bool uiBackdrop(SDL_Renderer* renderer, bool dim = false);
bool uiPanel(SDL_Renderer* renderer, Rect bounds, SDL_Color color, bool selected = false,
             bool disabled = false);
} // namespace cubeshot::client
