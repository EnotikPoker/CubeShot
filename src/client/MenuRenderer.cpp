#include "client/MenuRenderer.h"
#include "client/BitmapFont.h"
#include "client/Menu.h"
#include "network/Identity.h"

#include <SDL3/SDL.h>
#include <algorithm>
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

bool wheel(SDL_Renderer* renderer, const WorldSnapshot& state, PlayerId own)
{
    if (!own)
        return true;
    const auto slot = own - 1;
    const bool spinning = state.weaponChoice[slot] == WeaponChoice::Spinning;
    const auto result = spinning ? state.rolled[slot] : state.loadout[slot];
    const float elapsed = Match::spinSeconds - state.spinRemaining[slot];
    const float progress = spinning ? std::clamp(elapsed / Match::wheelSeconds, 0.f, 1.f) : 1.f;
    const float destination = weaponCount * 4.f + static_cast<unsigned>(result);
    const float position = destination - std::pow(1 - progress, 3.f) * weaponCount * 4;
    constexpr float width = 154;
    const SDL_Color colors[]{{75, 130, 175, 255}, {170, 100, 50, 255}, {75, 150, 95, 255},
                             {140, 85, 175, 255}, {180, 150, 60, 255}, {65, 150, 160, 255},
                             {185, 70, 95, 255}};
    if (!rectangle(renderer, {{166, 197}, {628, 152}}, {12, 18, 29, 255}))
        return false;
    const SDL_Rect clip{174, 205, 612, 136};
    SDL_SetRenderClipRect(renderer, &clip);
    bool ok = true;
    const int middle = static_cast<int>(std::floor(position));
    for (int i = middle - 3; i <= middle + 3; ++i)
    {
        const unsigned index =
            static_cast<unsigned>((i % static_cast<int>(weaponCount) + weaponCount) % weaponCount);
        const auto kind = static_cast<WeaponKind>(index);
        const float x = 480 + (i - position) * width;
        const bool selected = std::abs(x - 480) < width / 2;
        const auto color = colors[index % 7];
        ok =
            ok &&
            rectangle(renderer, {{x - 70, 211}, {140, 124}}, selected ? color : SDL_Color{40, 50, 66, 255}) &&
            rectangle(renderer, {{x - 67, 214}, {134, 118}}, {23, 31, 45, 255}) &&
            rectangle(renderer, {{x - 67, 323}, {134, 9}}, color);
        // Large silhouettes identify the cards while their names pass below the pointer.
        const float barrel = std::min(78.f, weaponDefinition(kind).barrel);
        if (meleeWeapon(kind))
            ok = ok &&
                 rectangle(renderer, {{x - 38, 256}, {74, kind == WeaponKind::Bat ? 12.f : 5.f}},
                           {215, 215, 205, 255}) &&
                 rectangle(renderer, {{x - 42, 252}, {20, 14}}, {150, 95, 45, 255});
        else if (kind == WeaponKind::FireBoomerang)
            ok = ok && rectangle(renderer, {{x - 33, 255}, {66, 10}}, {255, 155, 45, 255}) &&
                 rectangle(renderer, {{x + 23, 233}, {10, 32}}, {255, 210, 85, 255});
        else
            ok = ok &&
                 rectangle(renderer, {{x - 36, 249}, {barrel, kind == WeaponKind::Rpg ? 20.f : 12.f}},
                           color) &&
                 rectangle(renderer, {{x - 21, 258}, {13, 23}}, {165, 175, 185, 255});
        const auto name = weaponDefinition(kind).name;
        ok = ok && drawText(renderer, name, {x - textWidth(name, 1) / 2, 299}, 1, white);
    }
    SDL_SetRenderClipRect(renderer, nullptr);
    const auto shown =
        spinning && progress < 1
            ? static_cast<WeaponKind>((static_cast<unsigned>(std::lround(position))) % weaponCount)
            : result;
    const auto name = weaponDefinition(shown).name;
    const int scale = textWidth(name, 2) > 620 ? 1 : 2;
    return ok && rectangle(renderer, {{477, 189}, {6, 17}}, {255, 209, 90, 255}) &&
           rectangle(renderer, {{466, 185}, {28, 8}}, {255, 226, 140, 255}) &&
           centeredText(renderer, name, 363, scale, progress >= 1 ? SDL_Color{255, 220, 125, 255} : white);
}

bool preview(SDL_Renderer* renderer, MapId id, Rect box, bool test = true)
{
    const auto arena = Arena::create(id, test);
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
        if (menu.screen() == Screen::Result || menu.screen() == Screen::BotResult ||
            menu.screen() == Screen::Armory)
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
                case Screen::Armory:
                    title.clear();
                    hint.clear();
                    break;
                case Screen::BotSetup:
                    title = U"Игра с ботом";
                    hint = U"Выберите карту и сложность. Дуэль до 5 побед";
                    break;
                case Screen::BotResult:
                    title = U"Матч завершён";
                    hint = menu.networkSnapshot().winner == 1 ? U"Вы победили!" : U"Победил бот";
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
            if (menu.screen() == Screen::Armory)
            {
                if (!wheel(renderer, menu.networkSnapshot(), menu.ownId()))
                    return false;
                const auto other = menu.ownId() == 1 ? 1 : 0;
                const auto choice = menu.networkSnapshot().weaponChoice[other];
                if (!centeredText(renderer,
                                  choice == WeaponChoice::Ready      ? U"Соперник готов"
                                  : choice == WeaponChoice::Spinning ? U"Соперник меняет оружие"
                                                                     : U"Соперник выбирает",
                                  140, 1, muted))
                    return false;
            }
            if (menu.screen() == Screen::BotSetup)
            {
                if (!centeredText(renderer, mapInfo(menu.botMap()).name, 140, 2, blue) ||
                    !preview(renderer, menu.botMap(), {{392, 160}, {176, 99}}, false))
                    return false;
            }
            if (menu.screen() == Screen::BotResult)
            {
                const auto& s = menu.networkSnapshot();
                if (!centeredText(renderer, network::nicknameText(s.names[0]) + U" / Бот", 170, 2, white) ||
                    !centeredText(renderer,
                                  U"Счёт " + asciiText(std::to_string(s.score[0])) + U":" +
                                      asciiText(std::to_string(s.score[1])),
                                  210, 3, blue) ||
                    !centeredText(renderer, mapInfo(menu.botMap()).name, 250, 2, muted))
                    return false;
            }
            if (menu.screen() == Screen::Maps)
            {
                if (!centeredText(renderer, mapInfo(menu.selectedMap()).name, 145, 2, blue) ||
                    !preview(renderer, menu.selectedMap(), {{392, 160}, {176, 99}}))
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
                           menu.screen() == Screen::Maps || menu.screen() == Screen::Lobby ||
                           menu.screen() == Screen::BotSetup || menu.screen() == Screen::BotResult ||
                           menu.screen() == Screen::Armory)
                              ? 2
                              : 3,
                          button.placeholder ? muted : white))
            return false;
        if (button.placeholder && menu.screen() != Screen::Armory && menu.screen() != Screen::Lobby &&
            menu.screen() != Screen::Result && menu.screen() != Screen::Browser &&
            menu.screen() != Screen::OnlineBrowser &&
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
    const auto player =
        std::find_if(s.players.begin(), s.players.end(), [own](const auto& p) { return p.id == own; });
    if (player != s.players.end())
    {
        const auto label = weaponDefinition(player->weapon.kind).name;
        if (!drawText(renderer, label, {948 - textWidth(label, 2), 500}, 2, white))
            return false;
        if (player->weapon.kind == WeaponKind::Sniper &&
            !drawText(renderer, U"Удерживайте огонь 0.3 с", {690, 522}, 1, muted))
            return false;
    }
    if (!drawText(renderer, mapInfo(s.map).name, {12, 520}, 1, muted))
        return false;
    std::u32string status;
    if (s.phase == MatchPhase::Countdown)
        status = U"Начало через " + asciiText(std::to_string(static_cast<int>(std::ceil(s.remaining))));
    if (s.phase == MatchPhase::RoundEnd)
        status = (s.winner == 0     ? std::u32string(U"Ничья")
                  : s.winner == own ? std::u32string(U"Раунд выигран")
                                    : std::u32string(U"Раунд проигран")) +
                 U" — " + asciiText(std::to_string(static_cast<int>(std::ceil(s.remaining))));
    return status.empty() || centeredText(renderer, status, 125, 4, blue);
}

} // namespace cubeshot::client
