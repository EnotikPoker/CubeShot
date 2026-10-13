#include "client/MenuRenderer.h"
#include "client/BitmapFont.h"
#include "client/Menu.h"

#include <SDL3/SDL.h>

namespace cubeshot::client {
namespace {

constexpr SDL_Color white{232, 240, 255, 255};
constexpr SDL_Color muted{137, 153, 175, 255};
constexpr SDL_Color blue{80, 175, 255, 255};

bool rectangle(SDL_Renderer* renderer, const Rect& bounds, SDL_Color color)
{
    const SDL_FRect rect{bounds.position.x, bounds.position.y, bounds.size.x, bounds.size.y};
    return SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a) &&
           SDL_RenderFillRect(renderer, &rect);
}

bool centeredText(SDL_Renderer* renderer, std::u32string_view text, float y,
                  int scale, SDL_Color color)
{
    return drawText(renderer, text, {(960 - textWidth(text, scale)) / 2, y}, scale, color);
}

} // namespace

bool renderMenu(SDL_Renderer* renderer, const Menu& menu)
{
    if (menu.playing()) return true;
    if (menu.screen() == Screen::Paused) {
        if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) ||
            !rectangle(renderer, {{0, 0}, {960, 540}}, {8, 12, 20, 210}) ||
            !SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) ||
            !centeredText(renderer, U"Пауза", 105, 5, white) ||
            !centeredText(renderer, U"Игра приостановлена", 165, 2, muted)) return false;
    } else {
        if (!SDL_SetRenderDrawColor(renderer, 20, 25, 35, 255) || !SDL_RenderClear(renderer) ||
            !rectangle(renderer, {{90, 245}, {80, 80}}, {28, 41, 57, 255}) ||
            !rectangle(renderer, {{790, 245}, {80, 80}}, {28, 41, 57, 255}) ||
            !rectangle(renderer, {{0, 532}, {960, 8}}, {39, 70, 99, 255})) return false;
        if (menu.screen() == Screen::Main) {
            if (!centeredText(renderer, U"CubeShot", 54, 8, blue) ||
                !centeredText(renderer, U"Главное меню", 134, 2, muted)) return false;
        } else if (!centeredText(renderer, U"Выбор режима", 81, 4, white) ||
                   !centeredText(renderer, U"Тест — текущая сцена", 130, 2, muted)) return false;
    }
    const auto buttons = menu.buttons();
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        const auto& button = buttons[i];
        const bool selected = static_cast<int>(i) == menu.selection();
        const Rect& bounds = button.bounds;
        if (!rectangle(renderer, bounds, selected ? blue : SDL_Color{52, 64, 83, 255}) ||
            !rectangle(renderer, {{bounds.position.x + 2, bounds.position.y + 2},
                                  {bounds.size.x - 4, bounds.size.y - 4}},
                       selected ? SDL_Color{34, 56, 78, 255} : SDL_Color{29, 36, 49, 255}) ||
            !centeredText(renderer, button.label, bounds.position.y + 16, 3,
                          button.placeholder ? muted : white)) return false;
        if (button.placeholder &&
            !drawText(renderer, U"Скоро", {bounds.position.x + bounds.size.x - 44,
                                       bounds.position.y + 40}, 1, muted)) return false;
    }
    const auto footer = menu.message().empty()
        ? (menu.screen() == Screen::Main ? U"Мышь / стрелки / Enter" : U"Esc — назад")
        : menu.message();
    return centeredText(renderer, footer, 510, 2, muted);
}

} // namespace cubeshot::client
