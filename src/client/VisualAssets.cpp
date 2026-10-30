#include "client/VisualAssets.h"
#include <atomic>
#include <stdexcept>
#include <string>

namespace cubeshot::client
{
namespace
{
constexpr const char* property = "cubeshot.visual.assets";

SDL_Texture* load(SDL_Renderer* renderer, const std::filesystem::path& file)
{
    auto* surface = SDL_LoadPNG(file.u8string().c_str());
    if (!surface)
        throw std::runtime_error("Cannot load " + file.u8string() + ": " + SDL_GetError());
    auto* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (!texture)
        throw std::runtime_error(SDL_GetError());
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

bool fill(SDL_Renderer* r, Rect b, SDL_Color c)
{
    SDL_FRect box{b.position.x, b.position.y, b.size.x, b.size.y};
    return SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a) && SDL_RenderFillRect(r, &box);
}
} // namespace

std::filesystem::path VisualAssets::directory()
{
    const auto* base = SDL_GetBasePath();
    return (base ? std::filesystem::u8path(base) : std::filesystem::path{}) / "assets";
}

VisualAssets::VisualAssets(SDL_Renderer* r) : renderer_(r)
{
    static std::atomic<std::uint64_t> nextIdentity{1};
    identity = nextIdentity++;
    try
    {
        const auto root = directory();
        effects = load(r, root / "effects.png");
        materials = load(r, root / "materials.png");
        font = load(r, root / "font.png");
        SDL_SetPointerProperty(SDL_GetRendererProperties(r), property, this);
    }
    catch (...)
    {
        SDL_DestroyTexture(effects);
        SDL_DestroyTexture(materials);
        SDL_DestroyTexture(font);
        throw;
    }
}

VisualAssets::~VisualAssets()
{
    SDL_ClearProperty(SDL_GetRendererProperties(renderer_), property);
    SDL_DestroyTexture(effects);
    SDL_DestroyTexture(materials);
    SDL_DestroyTexture(font);
}

VisualAssets* VisualAssets::get(SDL_Renderer* r)
{
    return static_cast<VisualAssets*>(
        SDL_GetPointerProperty(SDL_GetRendererProperties(r), property, nullptr));
}

bool uiBackdrop(SDL_Renderer* r, bool dim)
{
    if (!SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE) || !SDL_SetRenderDrawColor(r, 22, 31, 47, 255) ||
        !SDL_RenderClear(r))
        return false;
    // Quiet broad shapes, no busy illustrated scenery behind controls.
    for (int y = 0; y < 540; y += 8)
        if (!fill(r, {{0, static_cast<float>(y)}, {960, 8}},
                  {static_cast<Uint8>(22 + y / 36), static_cast<Uint8>(31 + y / 25),
                   static_cast<Uint8>(47 + y / 24), 255}))
            return false;
    if (!fill(r, {{0, 440}, {960, 100}}, {20, 30, 44, 255}) ||
        !fill(r, {{0, 436}, {960, 4}}, {54, 73, 91, 255}))
        return false;
    for (float x : {42.f, 768.f})
        if (!fill(r, {{x, 280}, {150, 12}}, {48, 64, 80, 255}) ||
            !fill(r, {{x + 8, 292}, {8, 144}}, {26, 39, 55, 255}))
            return false;
    if (!dim)
    {
        if (!uiPanel(r, {{99, 370}, {55, 62}}, {50, 164, 239, 255}) ||
            !uiPanel(r, {{807, 370}, {55, 62}}, {231, 68, 79, 255}))
            return false;
        if (!fill(r, {{136, 386}, {7, 8}}, {248, 246, 235, 255}) ||
            !fill(r, {{816, 386}, {7, 8}}, {248, 246, 235, 255}))
            return false;
    }
    return true;
}

bool uiPanel(SDL_Renderer* r, Rect b, SDL_Color color, bool selected, bool disabled)
{
    auto* assets = VisualAssets::get(r);
    if (!assets)
        return false;
    if (disabled)
    {
        color.r = static_cast<Uint8>(color.r * .55f);
        color.g = static_cast<Uint8>(color.g * .55f);
        color.b = static_cast<Uint8>(color.b * .55f);
    }
    if (!fill(r, {{b.position.x + 3, b.position.y + 5}, b.size}, {9, 16, 26, 255}) ||
        !fill(r, b, {9, 16, 26, 255}))
        return false;
    SDL_FRect inner{b.position.x + 3, b.position.y + 3, b.size.x - 6, b.size.y - 6};
    SDL_SetTextureColorMod(assets->materials, color.r, color.g, color.b);
    float w = 0, h = 0;
    SDL_GetTextureSize(assets->materials, &w, &h);
    const SDL_FRect source{2, h / 2 + 2, w / 4 - 4, h / 2 - 4};
    if (!SDL_RenderTexture(r, assets->materials, &source, &inner))
        return false;
    if (!fill(r, {{b.position.x + 5, b.position.y + 4}, {b.size.x - 10, 2}},
              selected ? SDL_Color{255, 224, 143, 255} : SDL_Color{142, 164, 177, 255}))
        return false;
    if (selected && !fill(r, {{b.position.x + 3, b.position.y + 3}, {4, b.size.y - 6}}, {255, 200, 77, 255}))
        return false;
    return true;
}
} // namespace cubeshot::client
