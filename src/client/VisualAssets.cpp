#include "client/VisualAssets.h"
#include "client/TextRenderer.h"
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
        text = std::make_unique<TextRenderer>(r);
        SDL_SetPointerProperty(SDL_GetRendererProperties(r), property, this);
    }
    catch (...)
    {
        SDL_DestroyTexture(effects);
        SDL_DestroyTexture(materials);
        text.reset();
        throw;
    }
}

VisualAssets::~VisualAssets()
{
    SDL_ClearProperty(SDL_GetRendererProperties(renderer_), property);
    SDL_DestroyTexture(effects);
    SDL_DestroyTexture(materials);
    text.reset();
}

VisualAssets* VisualAssets::get(SDL_Renderer* r)
{
    return static_cast<VisualAssets*>(
        SDL_GetPointerProperty(SDL_GetRendererProperties(r), property, nullptr));
}

bool uiBackdrop(SDL_Renderer* r, bool)
{
    return SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND) && SDL_SetRenderDrawColor(r, 17, 22, 29, 255) &&
           SDL_RenderClear(r);
}

bool uiPanel(SDL_Renderer* r, Rect box, SDL_Color, bool selected, bool disabled)
{
    const SDL_Color border = selected ? SDL_Color{94, 177, 255, 255} : SDL_Color{44, 56, 70, 255};
    const SDL_Color face = disabled   ? SDL_Color{22, 28, 36, 255}
                           : selected ? SDL_Color{31, 48, 65, 255}
                                      : SDL_Color{26, 34, 45, 255};
    return fill(r, box, border) &&
           fill(r, {{box.position.x + 1, box.position.y + 1}, {box.size.x - 2, box.size.y - 2}}, face);
}
} // namespace cubeshot::client
