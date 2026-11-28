#include "client/BuildInspector.h"
#include "client/AbilityUI.h"
#include "client/UiText.h"
#include "game/AttackStats.h"
#include <algorithm>
#include <cmath>

namespace cubeshot::client
{
namespace
{
std::u32string asciiText(const std::string& s)
{
    return {s.begin(), s.end()};
}
} // namespace

bool drawBuildInspector(SDL_Renderer* r, const std::vector<Player>& players,
                        const std::array<std::u32string, 2>& names, const std::array<int, 2>& scroll,
                        Vec2 mouse)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 10, 17, 27, 245);
    SDL_FRect panel{16, 16, 928, 452};
    bool ok = SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    std::u32string description = U"Tab — закрыть · Влево/вправо — участник · Колёсико — список";
    for (unsigned slot = 0; slot < 2 && slot < players.size(); ++slot)
    {
        const auto& p = players[slot];
        float x = 30 + slot * 468.f;
        ok = ok && drawText(r, names[slot], {x, 28}, 2, {226, 237, 247, 255});
        for (unsigned a = 0; a < 2; ++a)
        {
            auto active = p.actives[a];
            auto label = (a ? U"E · " : U"Q · ") + std::u32string(activeDefinition(active.kind).name);
            if (active.cooldown > 0)
                label += U" " + asciiText(std::to_string(int(std::ceil(active.cooldown)))) + U" с";
            ok = ok && drawActiveIcon(r, active.kind, {{x + a * 224, 61}, {18, 18}}) &&
                 drawText(r, label, {x + 24 + a * 224, 62}, 1, {126, 204, 235, 255});
        }
        std::vector<AbilityKind> owned;
        for (unsigned i = 0; i < abilityCount; ++i)
            if (p.abilities.levels[i])
                owned.push_back(AbilityKind(i));
        int start = std::clamp(scroll[slot], 0, std::max(0, int(owned.size()) - 10));
        for (int row = 0; row < 10 && start + row < int(owned.size()); ++row)
        {
            auto k = owned[start + row];
            float y = 94 + row * 31.f;
            bool useful = passiveApplies(k, p.weapon.kind, {p.actives[0].kind, p.actives[1].kind});
            auto label = std::u32string(abilityDefinition(k).name) + U" · " +
                         asciiText(std::to_string(p.abilities.level(k)));
            ok = ok && drawAbilityIcon(r, k, {{x, y}, {20, 20}}) &&
                 drawText(r, label, {x + 30, y + 2}, 1,
                          useful ? SDL_Color{222, 235, 245, 255} : SDL_Color{105, 119, 137, 255});
            if (mouse.x >= x && mouse.x < x + 440 && mouse.y >= y && mouse.y < y + 30)
                description =
                    abilityEffect(k, p.abilities) + (useful ? U"" : U" · Не действует с текущим оружием");
        }
        ok = ok && drawText(r,
                            asciiText(std::to_string(owned.empty() ? 0 : start + 1)) + U"–" +
                                asciiText(std::to_string(std::min(start + 10, int(owned.size())))) + U" / " +
                                asciiText(std::to_string(owned.size())),
                            {x, 408}, 1, {142, 164, 189, 255});
    }
    if (textWidth(r, description, 1) > 890)
    {
        while (description.size() > 1 && textWidth(r, description + U"…", 1) > 890)
            description.pop_back();
        description += U"…";
    }
    return ok && drawText(r, description, {30, 441}, 1, {180, 198, 216, 255});
}
} // namespace cubeshot::client
