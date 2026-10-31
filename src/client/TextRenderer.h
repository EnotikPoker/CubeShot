#pragma once
#include "game/Types.h"
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

namespace cubeshot::client
{
// Owns renderer-bound text/glyph caches. Text is rasterized at output-pixel size.
class TextRenderer
{
public:
    explicit TextRenderer(SDL_Renderer* renderer);
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;
    float width(std::u32string_view text, int scale);
    float height(int scale);
    bool draw(std::u32string_view text, Vec2 at, int scale, SDL_Color color);

private:
    float outputScale(SDL_FRect* area = nullptr) const;
    TTF_Font* font(int scale);
    TTF_Text* text(std::u32string_view value, int scale);
    void clearText();
    SDL_Renderer* renderer_;
    TTF_TextEngine* engine_ = nullptr;
    bool initialized_ = false;
    std::map<std::pair<int, bool>, TTF_Font*> fonts_;
    std::map<std::tuple<int, bool, std::string>, TTF_Text*> texts_;
};
} // namespace cubeshot::client
