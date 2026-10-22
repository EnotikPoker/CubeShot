#include "client/MenuRenderer.h"
#include "client/BitmapFont.h"
#include "client/Menu.h"
#include "network/Identity.h"

#include <SDL3/SDL.h>
#include <cmath>

namespace cubeshot::client
{
namespace
{

constexpr SDL_Color white{232, 240, 255, 255};
constexpr SDL_Color muted{137, 153, 175, 255};
constexpr SDL_Color blue{80, 175, 255, 255};

bool rectangle(SDL_Renderer* renderer, const Rect& bounds, SDL_Color color)
{
    const SDL_FRect rect{bounds.position.x, bounds.position.y, bounds.size.x, bounds.size.y};
    return SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a) &&
           SDL_RenderFillRect(renderer, &rect);
}

bool centeredText(SDL_Renderer* renderer, std::u32string_view text, float y, int scale, SDL_Color color)
{
    return drawText(renderer, text, {(960 - textWidth(text, scale)) / 2, y}, scale, color);
}

bool preview(SDL_Renderer* renderer, MapId id, Rect box)
{
    const auto arena = Arena::create(id, true);
    const float scale = box.size.x / arena.bounds.size.x;
    if (!rectangle(renderer, box, {14, 21, 31, 255}))
        return false;
    for (const auto& platform : arena.platforms)
    {
        Rect r{{box.position.x + platform.position.x * scale, box.position.y + platform.position.y * scale},
               {platform.size.x * scale, platform.size.y * scale}};
        if (!rectangle(renderer, r, {115, 139, 154, 255}))
            return false;
    }
    for (const auto& spawn : arena.spawnPoints)
        if (!rectangle(renderer,
                       {{box.position.x + spawn.x * scale, box.position.y + spawn.y * scale},
                        {48 * scale, 48 * scale}},
                       {80, 175, 255, 255}))
            return false;
    return true;
}
} // namespace

bool renderMenu(SDL_Renderer* renderer, const Menu& menu)
{
    if (menu.playing())
        return true;
    if (menu.screen() == Screen::Paused)
    {
        if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) ||
            !rectangle(renderer, {{0, 0}, {960, 540}}, {8, 12, 20, 210}) ||
            !SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE) ||
            !centeredText(renderer, U"Пауза", 105, 5, white) ||
            !centeredText(renderer, (menu.lan() ? U"Матч продолжается. Вы уязвимы" : U"Игра приостановлена"),
                          165, 2, muted))
            return false;
    }
    else
    {
        if (menu.screen() == Screen::Result)
        {
            if (!SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND) ||
                !rectangle(renderer, {{0, 0}, {960, 540}}, {8, 12, 20, 210}) ||
                !SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE))
                return false;
        }
        else if (!SDL_SetRenderDrawColor(renderer, 20, 25, 35, 255) || !SDL_RenderClear(renderer))
            return false;
        if (!rectangle(renderer, {{90, 245}, {80, 80}}, {28, 41, 57, 255}) ||
            !rectangle(renderer, {{790, 245}, {80, 80}}, {28, 41, 57, 255}) ||
            !rectangle(renderer, {{0, 532}, {960, 8}}, {39, 70, 99, 255}))
            return false;
        if (menu.screen() == Screen::Main)
        {
            if (!centeredText(renderer, U"CubeShot", 54, 8, blue) ||
                !centeredText(renderer, U"Главное меню", 134, 2, muted))
                return false;
        }
        else
        {
            std::u32string title = U"Выбор режима", hint = U"Тест — карты с неподвижными мишенями";
            switch (menu.screen())
            {
                case Screen::Online:
                    title = U"Мультиплеер";
                    hint = U"Ник: " + network::nicknameText(menu.nickname());
                    break;
                case Screen::OnlineBrowser:
                    title = U"Серверы интернета";
                    hint = U"Выберите сервер или вернитесь к быстрому матчу";
                    break;
                case Screen::Lan:
                    title = U"Локальная сеть";
                    hint = U"Дуэль на двух компьютерах в одной сети";
                    break;
                case Screen::Browser:
                    title = U"Комнаты LAN";
                    hint = U"Поиск обновляется каждые 2 секунды";
                    break;
                case Screen::Address:
                    title = U"Подключение по IP";
                    hint = U"IPv4:порт. Порт по умолчанию 27015";
                    break;
                case Screen::Connecting:
                    title = U"Подключение...";
                    hint = menu.online() ? U"Бронирование и подключение к серверу"
                                         : U"Ожидание сервера, до 5 секунд";
                    break;
                case Screen::Maps:
                    title = U"Выбор карты";
                    hint = U"Тест: три мишени без ИИ";
                    break;
                case Screen::Lobby:
                    title = menu.online() ? U"Лобби интернета" : U"Лобби LAN";
                    hint = menu.networkSnapshot().voting
                               ? U"Выберите карту, затем оба нажмите Готов"
                               : U"Карту выбирает хозяин. Для старта оба нажимают Готов";
                    break;
                case Screen::Result:
                    title = U"Матч завершён";
                    hint =
                        menu.networkSnapshot().winner == menu.ownId() ? U"Вы победили!" : U"Победил соперник";
                    break;
                default:
                    break;
            }
            if (!centeredText(renderer, title, 65, 4, white) || !centeredText(renderer, hint, 117, 2, muted))
                return false;
            if (menu.screen() == Screen::Maps)
            {
                if (!centeredText(renderer, mapInfo(menu.selectedMap()).name, 145, 2, blue) ||
                    !preview(renderer, menu.selectedMap(), {{360, 165}, {240, 135}}))
                    return false;
            }
            if (menu.screen() == Screen::Address)
            {
                if (!rectangle(renderer, {{240, 165}, {480, 64}}, {34, 56, 78, 255}) ||
                    !centeredText(renderer, asciiText(menu.address()) + U"_", 188, 3, white) ||
                    !centeredText(renderer, U"Введите адрес. Ctrl-V — вставить", 247, 2, muted))
                    return false;
            }
            if (menu.screen() == Screen::Lobby || menu.screen() == Screen::Result)
            {
                const auto& s = menu.networkSnapshot();
                for (unsigned i = 0; i < 2; ++i)
                {
                    const auto label = network::nicknameText(s.names[i]) +
                                       (i + 1 == menu.ownId() ? U" / Вы" : U"") +
                                       (!s.connected[i] ? U" — ожидается"
                                        : s.ready[i]    ? U" — готов"
                                                        : U" — не готов");
                    if (!centeredText(renderer, label, 160 + i * 30, 2, white))
                        return false;
                }
                const auto score = U"Счёт " + asciiText(std::to_string(s.score[0])) + U":" +
                                   asciiText(std::to_string(s.score[1])) + U". До 5 побед";
                if (!centeredText(renderer, mapInfo(menu.selectedMap()).name, 225, 2, blue) ||
                    !preview(renderer, menu.selectedMap(), {{35, 162}, {190, 106.875f}}))
                    return false;
                if (s.voting && menu.screen() == Screen::Lobby)
                {
                    auto vote = [&](int index)
                    {
                        return s.votes[index] < 0
                                   ? std::u32string(U"не выбран")
                                   : std::u32string(mapInfo(static_cast<MapId>(s.votes[index])).name);
                    };
                    if (!centeredText(renderer, U"Голоса: " + vote(0) + U" / " + vote(1), 252, 1, muted))
                        return false;
                }
                if (!centeredText(renderer, score, 277, 2, blue))
                    return false;
            }
        }
    }
    const auto buttons = menu.buttons();
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
        const auto& button = buttons[i];
        const bool selected = static_cast<int>(i) == menu.selection();
        const Rect& bounds = button.bounds;
        if (!rectangle(renderer, bounds, selected ? blue : SDL_Color{52, 64, 83, 255}) ||
            !rectangle(
                renderer,
                {{bounds.position.x + 2, bounds.position.y + 2}, {bounds.size.x - 4, bounds.size.y - 4}},
                selected ? SDL_Color{34, 56, 78, 255} : SDL_Color{29, 36, 49, 255}) ||
            !centeredText(renderer, button.label, bounds.position.y + 16,
                          (menu.screen() == Screen::Browser || menu.screen() == Screen::OnlineBrowser ||
                           menu.screen() == Screen::Maps || menu.screen() == Screen::Lobby)
                              ? 2
                              : 3,
                          button.placeholder ? muted : white))
            return false;
        if (button.placeholder && menu.screen() != Screen::Lobby && menu.screen() != Screen::Result &&
            menu.screen() != Screen::Browser && menu.screen() != Screen::OnlineBrowser &&
            !drawText(renderer, U"Скоро", {bounds.position.x + bounds.size.x - 44, bounds.position.y + 40}, 1,
                      muted))
            return false;
    }
    const auto footer =
        menu.message().empty()
            ? (menu.screen() == Screen::Main                                       ? U"Мышь / стрелки / Enter"
               : menu.screen() == Screen::Lobby || menu.screen() == Screen::Result ? U"Esc — выйти из комнаты"
               : menu.screen() == Screen::Connecting                               ? U"Esc — отмена"
               : menu.screen() == Screen::Paused                                   ? U"Esc — продолжить"
                                                                                   : U"Esc — назад")
            : menu.message();
    return centeredText(renderer, footer, 510, 2, muted);
}

bool renderMatchHud(SDL_Renderer* renderer, const WorldSnapshot& s, PlayerId own)
{
    auto score = network::nicknameText(s.names[0]) + U" " + asciiText(std::to_string(s.score[0])) + U" : " +
                 asciiText(std::to_string(s.score[1])) + U" " + network::nicknameText(s.names[1]);
    if (!centeredText(renderer, score, 20, 2, white) ||
        !centeredText(renderer, U"Вы — игрок " + asciiText(std::to_string(own)) + U". До 5 побед", 51, 2,
                      muted))
        return false;
    if (!drawText(renderer, mapInfo(s.map).name, {12, 520}, 1, muted))
        return false;
    std::u32string status;
    if (s.phase == MatchPhase::Countdown)
        status = U"Начало через " + asciiText(std::to_string(static_cast<int>(std::ceil(s.remaining))));
    if (s.phase == MatchPhase::RoundEnd)
        status = s.winner == 0 ? U"Ничья" : s.winner == own ? U"Раунд выигран" : U"Раунд проигран";
    return status.empty() || centeredText(renderer, status, 125, 4, blue);
}

} // namespace cubeshot::client
