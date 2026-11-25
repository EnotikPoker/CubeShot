#include "client/UiText.h"
#include "client/AbilityUI.h"
#include "client/TextRenderer.h"
#include "client/VisualAssets.h"
#include "game/Player.h"
#include <algorithm>
#include <string>

namespace cubeshot::client
{
bool drawWeaponHud(SDL_Renderer* r, const Player& p)
{
    SDL_SetRenderDrawColor(r, 17, 26, 37, 255);
    SDL_FRect footer{0, 476, 960, 64};
    SDL_RenderFillRect(r, &footer);
    if (!drawAbilityHud(r, p))
        return false;
    const auto& d = weaponDefinition(p.weapon.kind);
    std::u32string name(d.name);
    if (!drawText(r, name, {948 - textWidth(r, name, 1), 482}, 1, {224, 235, 246, 255}))
        return false;
    auto ammo = d.capacity ? std::to_string(p.weapon.ammo) + "/" + std::to_string(p.weapon.capacity()) : "--";
    std::u32string label(ammo.begin(), ammo.end());
    if (!drawText(r, label, {948 - textWidth(r, label, 2), 505}, 2, {232, 241, 248, 255}))
        return false;
    if (p.weapon.reloading)
    {
        SDL_FRect bar{744, 532,
                      204 * (1 - std::clamp(p.weapon.reloadRemaining /
                                                (p.weapon.reloadDuration() + p.weapon.reloadSetup()),
                                            0.f, 1.f)),
                      3};
        SDL_SetRenderDrawColor(r, 94, 177, 255, 255);
        SDL_RenderFillRect(r, &bar);
    }
    if (p.weapon.kind == WeaponKind::Minigun)
    {
        SDL_FRect bar{744, 532, 204 * p.weapon.heat, 3};
        SDL_SetRenderDrawColor(r, 245, 161, 75, 255);
        SDL_RenderFillRect(r, &bar);
    }
    return true;
}

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
