#include "client/AbilityUI.h"
#include "client/UiText.h"
#include "game/AttackStats.h"
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
        case AbilityKind::Power:
            return rect(13, 3, 6, 26) && rect(3, 13, 26, 6);
        case AbilityKind::FireRate:
            return line(2, 10, 13, 10) && line(2, 16, 18, 16) && line(2, 22, 13, 22) && rect(20, 8, 8, 16);
        case AbilityKind::ProjectileSpeed:
            return line(2, 8, 12, 8) && line(2, 16, 15, 16) && line(2, 24, 12, 24) && line(18, 5, 29, 16) &&
                   line(29, 16, 18, 27);
        case AbilityKind::Range:
            return line(3, 16, 29, 16) && line(3, 9, 3, 23) && line(29, 9, 29, 23) && line(13, 8, 20, 16) &&
                   line(20, 16, 13, 24);
        case AbilityKind::Accuracy:
            return line(16, 2, 16, 11) && line(16, 21, 16, 30) && line(2, 16, 11, 16) &&
                   line(21, 16, 30, 16) && rect(14, 14, 4, 4);
        case AbilityKind::TwinShot:
            return rect(3, 7, 20, 5) && rect(3, 20, 20, 5) && line(23, 7, 29, 10) && line(23, 20, 29, 23);
        case AbilityKind::Ricochet:
            return line(3, 25, 20, 8) && line(20, 8, 28, 22) && line(11, 5, 29, 5) && line(23, 19, 28, 22) &&
                   line(28, 22, 30, 16);
        case AbilityKind::Penetration:
            return rect(13, 3, 5, 26) && line(2, 16, 30, 16) && line(23, 10, 30, 16) && line(30, 16, 23, 22);
        case AbilityKind::Demolition:
            return rect(3, 16, 11, 12) && rect(17, 16, 12, 12) && rect(3, 3, 11, 10) && line(20, 2, 16, 13) &&
                   line(16, 13, 27, 8);
        case AbilityKind::BlastRadius:
            return line(16, 1, 16, 8) && line(16, 24, 16, 31) && line(1, 16, 8, 16) && line(24, 16, 31, 16) &&
                   line(6, 6, 11, 11) && line(21, 21, 26, 26) && rect(12, 12, 8, 8);
        case AbilityKind::Vampirism:
            return line(5, 5, 9, 25) && line(9, 25, 16, 12) && line(16, 12, 23, 25) && line(23, 25, 27, 5) &&
                   rect(13, 24, 6, 6);
        case AbilityKind::Regeneration:
            return rect(13, 7, 6, 18) && rect(7, 13, 18, 6) && line(3, 4, 14, 1) && line(3, 4, 1, 15) &&
                   line(29, 28, 18, 31) && line(29, 28, 31, 17);
        case AbilityKind::ToughSkin:
            return line(4, 4, 28, 4) && line(28, 4, 28, 20) && line(28, 20, 16, 30) && line(16, 30, 4, 20) &&
                   line(4, 20, 4, 4) && rect(11, 11, 10, 10);
        case AbilityKind::FireResistance:
            return drawAbilityIcon(r, AbilityKind::Armor, box) && line(12, 22, 16, 9) && line(16, 9, 20, 22);
        case AbilityKind::Thorns:
            return line(2, 27, 7, 6) && line(7, 6, 14, 22) && line(14, 22, 22, 4) && line(22, 4, 30, 27) &&
                   line(2, 27, 30, 27);
        case AbilityKind::Adrenaline:
            return line(1, 17, 9, 17) && line(9, 17, 13, 5) && line(13, 5, 20, 28) && line(20, 28, 24, 17) &&
                   line(24, 17, 31, 17);
        case AbilityKind::DoubleJump:
            return line(4, 15, 10, 6) && line(10, 6, 16, 15) && line(16, 25, 23, 16) &&
                   line(23, 16, 30, 25) && line(10, 6, 10, 27) && line(23, 16, 23, 31);
        case AbilityKind::FeatherFall:
            return line(3, 7, 16, 3) && line(16, 3, 29, 7) && line(3, 7, 16, 22) && line(29, 7, 16, 22) &&
                   line(16, 22, 16, 30);
        case AbilityKind::Stability:
            return line(16, 2, 16, 29) && line(4, 22, 16, 29) && line(16, 29, 28, 22) && line(4, 22, 4, 17) &&
                   line(28, 22, 28, 17) && line(8, 9, 24, 9);
        case AbilityKind::SecondChance:
            return drawAbilityIcon(r, AbilityKind::Health, box) && line(2, 3, 10, 3) && line(2, 3, 2, 11) &&
                   line(30, 29, 22, 29) && line(30, 29, 30, 21);
        case AbilityKind::WideSwing:
            return line(3, 26, 16, 3) && line(16, 3, 29, 26) && line(3, 26, 29, 26);
        case AbilityKind::Lunge:
            return drawAbilityIcon(r, AbilityKind::Speed, box) && line(3, 2, 29, 2);
        case AbilityKind::Bleeding:
            return line(16, 2, 3, 23) && line(3, 23, 16, 30) && line(16, 30, 29, 23) && line(29, 23, 16, 2);
        case AbilityKind::Combo:
            return rect(3, 22, 6, 8) && rect(13, 14, 6, 16) && rect(23, 4, 6, 26);
        case AbilityKind::ReturnStrike:
            return line(29, 16, 3, 16) && line(3, 16, 12, 6) && line(3, 16, 12, 26);
        case AbilityKind::SecondCircuit:
            return drawAbilityIcon(r, AbilityKind::FastReload, box) && rect(13, 12, 5, 9);
        default:
            return true;
    }
}

bool drawAbilityRow(SDL_Renderer* r, const Abilities& abilities, Vec2 at, float size, const Player* owner)
{
    unsigned n = 0;
    for (unsigned i = 0; i < abilityCount; ++i)
        if (abilities.levels[i])
        {
            Rect box{{at.x + (n % 10) * (size + 7), at.y - (n / 10) * (size + 7)}, {size, size}};
            SDL_SetRenderDrawColor(r, 24, 36, 49, 255);
            SDL_FRect panel{box.position.x - 2, box.position.y - 2, size + 4, size + 4};
            if (!SDL_RenderFillRect(r, &panel) || !drawAbilityIcon(r, static_cast<AbilityKind>(i), box))
                return false;
            if (abilities.levels[i] > 1 &&
                !drawText(r, number(abilities.levels[i]),
                          {box.position.x + size - 4, box.position.y + size - 7}, 1, {246, 220, 151, 255}))
                return false;
            if (owner && !passiveApplies(AbilityKind(i), owner->weapon.kind,
                                         {owner->actives[0].kind, owner->actives[1].kind}))
            {
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(r, 12, 20, 30, 175);
                if (!SDL_RenderFillRect(r, &panel))
                    return false;
                SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
            }
            ++n;
        }
    return true;
}

bool drawAbilityHud(SDL_Renderer* r, const Player& p)
{
    if (!drawAbilityRow(r, p.abilities, {12, 474}, 20, &p))
        return false;
    auto text = U"HP " + number(p.health) + U"/" + number(p.healthLimit());
    if (p.abilities.level(AbilityKind::Armor))
        text += U"  Щиты " + number(p.shields);
    if (!drawText(r, text, {12, 503}, 1, {224, 235, 246, 255}))
        return false;
    if (p.abilities.level(AbilityKind::Jetpack))
    {
        if (!drawText(r, U"Топливо", {315, 444}, 1, {133, 203, 235, 255}))
            return false;
        SDL_SetRenderDrawColor(r, 35, 57, 74, 255);
        SDL_FRect bar{315, 461, 132, 3};
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

namespace cubeshot::client
{
bool drawActiveIcon(SDL_Renderer* r, ActiveKind kind, Rect box)
{
    static constexpr AbilityKind symbols[]{
        AbilityKind::None,  AbilityKind::Speed,       AbilityKind::SmallBody,
        AbilityKind::Armor, AbilityKind::Health,      AbilityKind::ReturnStrike,
        AbilityKind::Jump,  AbilityKind::BlastRadius, AbilityKind::Demolition};
    if (!validActive(kind))
        return false;
    if (kind == ActiveKind::None)
    {
        SDL_SetRenderDrawColor(r, 80, 98, 119, 255);
        return SDL_RenderLine(r, box.position.x, box.position.y + box.size.y / 2, box.position.x + box.size.x,
                              box.position.y + box.size.y / 2);
    }
    return drawAbilityIcon(r, symbols[unsigned(kind)], box);
}

bool drawActiveHud(SDL_Renderer* r, const Player& p, const std::array<std::u32string, 2>& keys)
{
    bool ok = true;
    for (unsigned i = 0; i < 2; ++i)
    {
        auto slot = p.actives[i];
        float y = 415 + i * 30.f;
        SDL_FRect bg{704, y, 244, 27};
        SDL_SetRenderDrawColor(r, 18, 28, 40, 225);
        ok = ok && SDL_RenderFillRect(r, &bg);
        ok = ok && drawActiveIcon(r, slot.kind, {{710, y + 4}, {19, 19}});
        std::u32string label = keys[i] + U" · " + std::u32string(activeDefinition(slot.kind).name);
        if (slot.cooldown > 0)
            label += U" " + number(unsigned(std::ceil(slot.cooldown))) + U" с";
        ok =
            ok && drawText(r, label, {737, y + 5}, 1,
                           slot.cooldown > 0 ? SDL_Color{125, 143, 163, 255} : SDL_Color{132, 215, 239, 255});
    }
    return ok;
}
} // namespace cubeshot::client
