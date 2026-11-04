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
    if (!drawAbilityHud(r, p))
        return false;
    const auto& d = weaponDefinition(p.weapon.kind);
    std::u32string label(d.name);
    if (d.capacity)
    {
        auto n = std::to_string(p.weapon.ammo) + "/" + std::to_string(p.weapon.capacity());
        label += U"  " + std::u32string(n.begin(), n.end()) + U" · ∞";
    }
    if (!drawText(r, label, {948 - textWidth(r, label, 2), 490}, 2, {235, 241, 247, 255}))
        return false;
    if (p.weapon.reloading)
    {
        if (!drawText(r, p.weapon.kind == WeaponKind::Flamethrower ? U"Смена баллона" : U"Перезарядка",
                      {740, 518}, 1, {94, 177, 255, 255}))
            return false;
        SDL_FRect bar{740, 536,
                      208 * (1 - std::clamp(p.weapon.reloadRemaining /
                                                (p.weapon.reloadDuration() + p.weapon.reloadSetup()),
                                            0.f, 1.f)),
                      2};
        SDL_SetRenderDrawColor(r, 94, 177, 255, 255);
        return SDL_RenderFillRect(r, &bar);
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
