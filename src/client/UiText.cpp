#include "client/UiText.h"
#include "client/TextRenderer.h"
#include "client/VisualAssets.h"

namespace cubeshot::client
{
float textWidth(SDL_Renderer* renderer, std::u32string_view text, int scale)
{
    auto* assets = VisualAssets::get(renderer);
    return assets ? assets->text->width(text, scale) : 0;
}

float textHeight(SDL_Renderer* renderer, int scale)
{
    auto* assets = VisualAssets::get(renderer);
    return assets ? assets->text->height(scale) : 0;
}

bool drawText(SDL_Renderer* renderer, std::u32string_view text, Vec2 at, int scale, SDL_Color color)
{
    auto* assets = VisualAssets::get(renderer);
    return assets && assets->text->draw(text, at, scale, color);
}
} // namespace cubeshot::client
