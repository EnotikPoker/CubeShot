#include "client/SettingsRenderer.h"
#include "client/SettingsController.h"
#include "client/UiText.h"
#include "client/VisualAssets.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace cubeshot::client
{
namespace
{
bool centered(SDL_Renderer* renderer, std::u32string_view text, float y, int scale, SDL_Color color)
{
    return drawText(renderer, text, {(960 - textWidth(renderer, text, scale)) / 2, y}, scale, color);
}
} // namespace

bool renderSettings(SDL_Renderer* renderer, const SettingsController& settings, std::uint64_t now,
                    bool networkMatch)
{
    const SDL_Color white{232, 240, 255, 255}, muted{150, 165, 185, 255}, blue{80, 175, 255, 255};
    if (!uiBackdrop(renderer, true) || !centered(renderer, U"Настройки", 24, 4, white))
        return false;
    if (settings.previewing())
    {
        const auto seconds = std::to_string(settings.remainingSeconds(now));
        if (!centered(renderer, U"Сохранить изменения экрана?", 170, 3, white) ||
            !centered(renderer, U"Возврат через " + std::u32string(seconds.begin(), seconds.end()) + U" с",
                      220, 2, muted))
            return false;
    }
    else if (settings.tab() == SettingsTab::Player)
    {
        if (!centered(renderer, U"Ник игрока", 140, 2, white) ||
            !centered(renderer, U"До 16 букв и цифр. Ctrl-A / Ctrl-V / Backspace", 255, 2, muted) ||
            !centered(renderer, U"Новый ник используется при следующем подключении", 290, 2, muted))
            return false;
    }
    else if (settings.tab() == SettingsTab::Controls)
    {
        for (int a = 0; a < 5; ++a)
            if (!drawText(renderer, actionName(static_cast<InputAction>(a)), {45, 151.f + a * 48}, 2, white))
                return false;
        if (!centered(renderer,
                      settings.capturing() ? U"Нажмите кнопку. Esc — отмена"
                                           : U"Выберите слот. X — очистить. Esc не переназначается",
                      395, 2, muted))
            return false;
    }
    const auto items = settings.items();
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& item = items[i];
        const auto& b = item.bounds;
        const bool selected =
            static_cast<int>(i) == settings.selection() ||
            (item.kind == SettingsItem::Kind::Tab && item.value == static_cast<int>(settings.tab()));
        const SDL_FRect box{b.position.x, b.position.y, b.size.x, b.size.y};
        if (!uiPanel(renderer, b, selected ? SDL_Color{56, 103, 145, 255} : SDL_Color{42, 60, 81, 255},
                     selected, !item.enabled))
            return false;
        auto label = item.label;
        const int scale = 2;
        if (textWidth(renderer, label, scale) > box.w - 14)
        {
            while (label.size() > 1 && textWidth(renderer, label + U"…", scale) > box.w - 14)
                label.pop_back();
            label += U"…";
        }
        if (!drawText(renderer, label,
                      {box.x + (box.w - textWidth(renderer, label, scale)) / 2,
                       box.y + (box.h - textHeight(renderer, scale)) / 2 - 1},
                      scale, item.enabled ? white : muted))
            return false;
    }
    if (!centered(renderer, settings.message(), 489, 2, blue))
        return false;
    return centered(renderer,
                    networkMatch ? U"Матч продолжается. Вы уязвимы"
                                 : U"Tab — вкладка. Стрелки / Enter. Esc — отмена",
                    518, 1, muted);
}
} // namespace cubeshot::client
