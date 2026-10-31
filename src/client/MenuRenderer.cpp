#include "client/MenuRenderer.h"
#include "client/Menu.h"
#include "client/UiText.h"
#include "client/VisualAssets.h"
#include "network/Identity.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
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
    return drawText(renderer, text, {(960 - textWidth(renderer, text, scale)) / 2, y}, scale, color);
}

bool weaponCard(SDL_Renderer* renderer, WeaponKind kind, float x)
{
    constexpr SDL_Color ink{8, 17, 27, 255}, steel{182, 204, 215, 255}, dark{63, 85, 104, 255},
        wood{164, 102, 55, 255};
    bool ok = true;
    auto part = [&](float left, float top, float width, float height, SDL_Color color)
    {
        ok = ok && rectangle(renderer, {{x + left - 2, 260 + top - 2}, {width + 4, height + 4}}, ink) &&
             rectangle(renderer, {{x + left, 260 + top}, {width, height}}, color);
    };
    auto round = [&](float cx, float cy, float radius, SDL_Color color)
    {
        for (int layer = 0; layer < 2; ++layer)
        {
            const float r = radius + (layer ? 0 : 2);
            const auto tint = layer ? color : ink;
            const SDL_FColor f{tint.r / 255.f, tint.g / 255.f, tint.b / 255.f, 1};
            std::array<SDL_Vertex, 36> vertices{};
            for (int n = 0; n < 12; ++n)
            {
                const float a = n * 6.2831853f / 12, b = (n + 1) * 6.2831853f / 12;
                vertices[n * 3] = {{x + cx, 260 + cy}, f, {}};
                vertices[n * 3 + 1] = {{x + cx + std::cos(a) * r, 260 + cy + std::sin(a) * r}, f, {}};
                vertices[n * 3 + 2] = {{x + cx + std::cos(b) * r, 260 + cy + std::sin(b) * r}, f, {}};
            }
            ok = ok && SDL_RenderGeometry(renderer, nullptr, vertices.data(),
                                          static_cast<int>(vertices.size()), nullptr, 0);
        }
    };
    if (kind == WeaponKind::FireBoomerang)
    {
        part(-37, -9, 40, 9, {255, 119, 39, 255});
        part(-5, -25, 10, 24, {255, 187, 55, 255});
        part(-36, -8, 28, 3, {255, 227, 124, 255});
        return ok;
    }
    if (meleeWeapon(kind))
    {
        const bool bat = kind == WeaponKind::Bat, knife = kind == WeaponKind::Knife;
        part(-43, -4, 22, 10, wood);
        part(-19, -(bat ? 8 : 4), knife ? 35 : 66, bat ? 18 : 7, bat ? wood : steel);
        if (!bat)
            part(-23, -10, 4, 22, {218, 177, 80, 255});
        if (!bat)
            part(-13, -3, knife ? 28 : 55, 2, {244, 249, 245, 255});
        return ok;
    }
    const bool longGun =
        kind == WeaponKind::Rifle || kind == WeaponKind::Sniper || kind == WeaponKind::Shotgun;
    if (kind == WeaponKind::Minigun)
    {
        round(-14, 8, 15, dark);
        part(-35, -10, 27, 20, dark);
        for (int n = 0; n < 3; ++n)
            part(-5, -10 + n * 7, 48, 4, steel);
        part(24, -15, 7, 30, dark);
        part(-24, 13, 8, 16, wood);
        return ok;
    }
    if (kind == WeaponKind::Rpg)
    {
        part(-47, -8, 77, 17, {100, 126, 70, 255});
        part(-43, -11, 8, 23, dark);
        part(31, -13, 15, 27, {156, 170, 78, 255});
        part(47, -7, 8, 15, {201, 188, 90, 255});
        part(-10, 11, 8, 18, wood);
        part(6, -19, 7, 10, dark);
        return ok;
    }
    if (kind == WeaponKind::Flamethrower)
    {
        round(-27, 11, 12, {184, 81, 48, 255});
        part(-33, -15, 17, 27, {222, 144, 67, 255});
        part(-8, -7, 42, 13, dark);
        part(33, -10, 13, 18, steel);
        part(45, -4, 9, 6, {255, 177, 49, 255});
        part(-4, 8, 8, 18, wood);
        return ok;
    }
    part(-28, 1, 11, 26, wood);
    if (longGun || kind == WeaponKind::Automatic)
        part(-49, -4, 20, 14, wood);
    const float length = kind == WeaponKind::Pistol ? 36 : kind == WeaponKind::Smg ? 45 : longGun ? 76 : 58;
    part(-29, -11, length, 12, dark);
    part(-25, -10, length - 5, 4, steel);
    if (kind == WeaponKind::Shotgun)
    {
        part(-2, 2, 25, 7, wood);
        part(25, -4, 25, 4, steel);
    }
    if (kind == WeaponKind::Revolver)
    {
        round(-15, -1, 10, steel);
        round(-15, -1, 3, dark);
        part(6, -10, 23, 8, steel);
    }
    if (kind == WeaponKind::Automatic || kind == WeaponKind::Smg)
        part(-2, 3, 9, kind == WeaponKind::Smg ? 22 : 18, dark);
    if (kind == WeaponKind::Rifle || kind == WeaponKind::Sniper)
    {
        part(-4, -21, 29, 6, dark);
        round(25, -18, 6, steel);
    }
    part(-20, -13, 5, 2, {239, 225, 168, 255});
    return ok;
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
        const auto color = selected ? SDL_Color{94, 177, 255, 255} : SDL_Color{54, 72, 89, 255};
        ok =
            ok &&
            rectangle(renderer, {{x - 70, 211}, {140, 124}}, selected ? color : SDL_Color{40, 50, 66, 255}) &&
            rectangle(renderer, {{x - 67, 214}, {134, 118}}, {23, 31, 45, 255}) &&
            rectangle(renderer, {{x - 67, 323}, {134, 9}}, color);
        ok = ok && weaponCard(renderer, kind, x);
        const auto name = weaponDefinition(kind).name;
        if (textWidth(renderer, name, 1) > 128 && name.find(U' ') != std::u32string_view::npos)
        {
            auto space = name.find(U' ');
            auto first = name.substr(0, space), second = name.substr(space + 1);
            ok = ok && drawText(renderer, first, {x - textWidth(renderer, first, 1) / 2, 286}, 1, white) &&
                 drawText(renderer, second, {x - textWidth(renderer, second, 1) / 2, 304}, 1, white);
        }
        else
            ok = ok && drawText(renderer, name, {x - textWidth(renderer, name, 1) / 2, 299}, 1, white);
    }
    SDL_SetRenderClipRect(renderer, nullptr);
    const auto shown =
        spinning && progress < 1
            ? static_cast<WeaponKind>((static_cast<unsigned>(std::lround(position))) % weaponCount)
            : result;
    const auto name = weaponDefinition(shown).name;
    const int scale = textWidth(renderer, name, 2) > 620 ? 1 : 2;
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
        else if (!uiBackdrop(renderer))
            return false;
        if (menu.screen() == Screen::Main)
        {
            if (!centeredText(renderer, U"CubeShot", 54, 8, {237, 242, 248, 255}) ||
                !centeredText(renderer, asciiText(CUBESHOT_VERSION), 139, 1, muted))
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
                                  choice == WeaponChoice::Ready ? U"Соперник готов"
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
        const int scale = (menu.screen() == Screen::Main || menu.screen() == Screen::Modes ||
                           menu.screen() == Screen::Paused)
                              ? 3
                              : 2;
        const SDL_Color face = selected ? SDL_Color{54, 103, 147, 255} : SDL_Color{41, 62, 86, 255};
        if (!uiPanel(renderer, bounds, face, selected, button.placeholder) ||
            !centeredText(renderer, button.label,
                          bounds.position.y + (bounds.size.y - textHeight(renderer, scale)) / 2 - 1, scale,
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
            ? (menu.screen() == Screen::Main ? U"Мышь / стрелки / Enter"
               : menu.screen() == Screen::Lobby || menu.screen() == Screen::Result ? U"Esc — выйти из комнаты"
               : menu.screen() == Screen::Connecting ? U"Esc — отмена"
               : menu.screen() == Screen::Paused     ? U"Esc — продолжить"
                                                     : U"Esc — назад")
            : menu.message();
    return centeredText(renderer, footer, 510, 2, muted);
}

bool renderMatchHud(SDL_Renderer* renderer, const WorldSnapshot& s, PlayerId own)
{
    auto score = network::nicknameText(s.names[0]) + U" " + asciiText(std::to_string(s.score[0])) + U" : " +
                 asciiText(std::to_string(s.score[1])) + U" " + network::nicknameText(s.names[1]);
    if (!uiPanel(renderer, {{235, 8}, {490, 69}}, {30, 46, 66, 255}) ||
        !centeredText(renderer, score, 14, textWidth(renderer, score, 2) > 466 ? 1 : 2, white) ||
        !centeredText(renderer, U"Вы — игрок " + asciiText(std::to_string(own)) + U". До 5 побед", 45, 1,
                      muted))
        return false;
    const auto player =
        std::find_if(s.players.begin(), s.players.end(), [own](const auto& p) { return p.id == own; });
    if (player != s.players.end())
    {
        const auto label = weaponDefinition(player->weapon.kind).name;
        if (!drawText(renderer, label, {948 - textWidth(renderer, label, 2), 500}, 2, white))
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
