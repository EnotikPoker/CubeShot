#include "client/AbilityUI.h"
#include "client/UiText.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace cubeshot::client
{
namespace
{
std::u32string number(unsigned n)
{
    auto s = std::to_string(n);
    return {s.begin(), s.end()};
}
} // namespace

bool drawAbilityIcon(SDL_Renderer* r, AbilityKind kind, Rect box)
{
    const float scale = box.size.x / 32;
    const SDL_Color tint = kind == AbilityKind::Health       ? SDL_Color{247, 111, 123, 255}
                           : kind == AbilityKind::Incendiary ? SDL_Color{255, 171, 83, 255}
                                                             : SDL_Color{120, 197, 235, 255};
    SDL_SetRenderDrawColor(r, tint.r, tint.g, tint.b, tint.a);
    auto line = [&](float a, float b, float c, float d)
    {
        return SDL_RenderLine(r, box.position.x + a * scale, box.position.y + b * scale,
                              box.position.x + c * scale, box.position.y + d * scale);
    };
    auto rect = [&](float x, float y, float w, float h)
    {
        SDL_FRect q{box.position.x + x * scale, box.position.y + y * scale, w * scale, h * scale};
        return SDL_RenderFillRect(r, &q);
    };
    switch (kind)
    {
        case AbilityKind::SmallBody:
            return line(3, 3, 13, 3) && line(3, 3, 3, 13) && line(29, 29, 19, 29) && line(29, 29, 29, 19) &&
                   rect(12, 12, 9, 9);
        case AbilityKind::Speed:
            return line(3, 8, 12, 16) && line(12, 16, 3, 24) && line(13, 8, 22, 16) && line(22, 16, 13, 24) &&
                   line(23, 8, 31, 16) && line(31, 16, 23, 24);
        case AbilityKind::Jump:
            return rect(14, 6, 4, 22) && line(5, 15, 16, 4) && line(16, 4, 27, 15);
        case AbilityKind::Health:
            return rect(12, 3, 8, 26) && rect(3, 12, 26, 8);
        case AbilityKind::LargeBullets:
            return rect(4, 14, 22, 7) && line(26, 14, 31, 17.5f) && line(31, 17.5f, 26, 21) &&
                   line(6, 5, 6, 10) && line(6, 5, 11, 5);
        case AbilityKind::Armor:
            return line(4, 5, 16, 2) && line(16, 2, 28, 5) && line(28, 5, 26, 21) && line(26, 21, 16, 30) &&
                   line(16, 30, 6, 21) && line(6, 21, 4, 5) && rect(14, 9, 4, 12);
        case AbilityKind::FastReload:
            return line(5, 12, 10, 4) && line(10, 4, 24, 4) && line(24, 4, 28, 12) && line(28, 12, 25, 24) &&
                   line(25, 24, 12, 28) && line(12, 28, 5, 22) && line(1, 14, 5, 22) && line(5, 22, 12, 18);
        case AbilityKind::Magazine:
            return rect(6, 7, 20, 23) && line(6, 3, 26, 3) && line(11, 11, 11, 25) && line(16, 11, 16, 25) &&
                   line(21, 11, 21, 25);
        case AbilityKind::Incendiary:
            return line(16, 2, 10, 14) && line(10, 14, 7, 10) && line(7, 10, 4, 22) && line(4, 22, 11, 30) &&
                   line(11, 30, 23, 29) && line(23, 29, 29, 21) && line(29, 21, 23, 9) &&
                   line(23, 9, 21, 19) && line(21, 19, 16, 2);
        case AbilityKind::Jetpack:
            return rect(5, 7, 8, 17) && rect(19, 7, 8, 17) && line(9, 25, 9, 32) && line(23, 25, 23, 32) &&
                   line(13, 10, 19, 10) && line(13, 20, 19, 20);
        case AbilityKind::Recoil:
            return rect(12, 9, 19, 6) && rect(15, 15, 5, 13) && line(8, 7, 1, 15) && line(1, 15, 8, 23) &&
                   line(13, 4, 6, 11);
        default:
            return true;
    }
}

bool drawAbilityRow(SDL_Renderer* r, const Abilities& abilities, Vec2 at, float size)
{
    unsigned n = 0;
    for (unsigned i = 0; i < abilityCount; ++i)
        if (abilities.levels[i])
        {
            Rect box{{at.x + n * (size + 7), at.y}, {size, size}};
            SDL_SetRenderDrawColor(r, 24, 36, 49, 255);
            SDL_FRect panel{box.position.x - 2, box.position.y - 2, size + 4, size + 4};
            if (!SDL_RenderFillRect(r, &panel) || !drawAbilityIcon(r, static_cast<AbilityKind>(i), box))
                return false;
            if (abilities.levels[i] > 1 &&
                !drawText(r, number(abilities.levels[i]),
                          {box.position.x + size - 4, box.position.y + size - 7}, 1, {246, 220, 151, 255}))
                return false;
            ++n;
        }
    return true;
}

bool drawAbilityHud(SDL_Renderer* r, const Player& p)
{
    if (!drawAbilityRow(r, p.abilities, {12, 474}, 20))
        return false;
    auto text = U"HP " + number(p.health) + U"/" + number(p.healthLimit());
    if (p.abilities.level(AbilityKind::Armor))
        text += U"  Щиты " + number(p.shields);
    if (!drawText(r, text, {12, 503}, 1, {224, 235, 246, 255}))
        return false;
    if (p.abilities.level(AbilityKind::Jetpack))
    {
        if (!drawText(r, U"Топливо", {12, 444}, 1, {133, 203, 235, 255}))
            return false;
        SDL_SetRenderDrawColor(r, 35, 57, 74, 255);
        SDL_FRect bar{12, 461, 132, 3};
        if (!SDL_RenderFillRect(r, &bar))
            return false;
        SDL_SetRenderDrawColor(r, 123, 211, 250, 255);
        bar.w *= std::clamp(p.jetpackFuel / 2, 0.f, 1.f);
        if (!SDL_RenderFillRect(r, &bar))
            return false;
    }
    return true;
}
} // namespace cubeshot::client
