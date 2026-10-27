#include "client/Menu.h"
#include "network/Identity.h"
#include <algorithm>

namespace cubeshot::client
{
std::u32string asciiText(std::string_view text)
{
    return {text.begin(), text.end()};
}

Screen Menu::screen() const
{
    return screen_;
}

bool Menu::playing() const
{
    return screen_ == Screen::Playing;
}

int Menu::selection() const
{
    return selection_;
}

std::u32string_view Menu::message() const
{
    return message_;
}

std::vector<MenuButton> Menu::buttons() const
{
    std::vector<MenuButton> result;
    switch (screen_)
    {
        case Screen::Main:
            result = {{U"Играть", {}}, {U"Настройки", {}}, {U"Выйти из игры", {}}};
            break;
        case Screen::Modes:
            result = {{U"Мультиплеер", {}},
                      {U"Локальная игра", {}},
                      {U"Игра с ботами", {}},
                      {U"Тест", {}},
                      {U"Назад", {}}};
            break;
        case Screen::Paused:
            result = {{U"Продолжить", {}},
                      {U"Настройки", {}},
                      {U"Выйти в главное меню", {}},
                      {U"Выйти из игры", {}}};
            break;
        case Screen::Lan:
            result = {{U"Создать комнату", {}}, {U"Найти комнаты", {}}, {U"Ввести IP", {}}, {U"Назад", {}}};
            break;
        case Screen::Browser:
            if (rooms_.empty())
                result.push_back({U"Поиск комнат...", {}, true});
            else
                for (std::size_t i = roomPage_ * 3; i < std::min(rooms_.size(), roomPage_ * 3 + 3); ++i)
                    result.push_back({labels_[i], {}, !rooms_[i].joinable});
            if (rooms_.size() > 3)
                result.push_back({U"Следующие комнаты", {}});
            result.push_back({U"Ввести IP", {}});
            result.push_back({U"Назад", {}});
            break;
        case Screen::Online:
            result = {{U"Быстрый матч", {}}, {U"Список серверов", {}}, {U"Назад", {}}};
            break;
        case Screen::OnlineBrowser:
            if (onlineRooms_.empty())
                result.push_back({U"Нет доступных серверов", {}, true});
            else
                for (std::size_t i = onlinePage_ * 3; i < std::min(onlineRooms_.size(), onlinePage_ * 3 + 3);
                     ++i)
                    result.push_back({onlineLabels_[i], {}, !onlineRooms_[i].joinable});
            if (onlineRooms_.size() > 3)
                result.push_back({U"Следующие серверы", {}});
            result.push_back({U"Обновить", {}});
            result.push_back({U"Назад", {}});
            break;
        case Screen::Address:
            result = {{U"Подключиться", {}}, {U"Назад", {}}};
            break;
        case Screen::Connecting:
            result = {{U"Отмена", {}}};
            break;
        case Screen::Maps:
            result = {{U"Предыдущая карта", {}},
                      {U"Следующая карта", {}},
                      {U"Начать тест", {}},
                      {U"Назад", {}},
                      {testWeaponLabel_, {}}};
            break;
        case Screen::Armory:
        {
            const auto choice = own_ ? snapshot_.weaponChoice[own_ - 1] : WeaponChoice::Pending;
            result = {{U"Сменить оружие", {}, choice != WeaponChoice::Pending},
                      {choice == WeaponChoice::Ready ? U"Ожидание соперника" : U"Продолжить",
                       {},
                       choice != WeaponChoice::Pending}};
            break;
        }
        case Screen::BotSetup:
            result = {{U"Предыдущая карта", {}},
                      {U"Следующая карта", {}},
                      {botDifficultyName(botDifficulty_), {}},
                      {U"Начать", {}},
                      {U"Назад", {}}};
            break;
        case Screen::BotResult:
            result = {{U"Реванш", {}}, {U"Карта и сложность", {}}, {U"Выйти в главное меню", {}}};
            break;
        case Screen::Lobby:
        case Screen::Result:
            result = {{own_ && snapshot_.ready[own_ - 1] ? U"Отменить готовность"
                       : screen_ == Screen::Result       ? U"Реванш"
                                                         : U"Готов",
                       {}},
                      {U"Сменить карту", {}, !snapshot_.voting && own_ != 1},
                      {U"Выйти из комнаты", {}}};
            if (screen_ == Screen::Lobby)
            {
                result[1] = {U"Предыдущая карта", {}, !snapshot_.voting && own_ != 1};
                result.insert(result.begin() + 2, {U"Следующая карта", {}, !snapshot_.voting && own_ != 1});
                if (snapshot_.voting && own_ && snapshot_.votes[own_ - 1] < 0)
                    result[0].label = U"Голосовать за карту";
                else if (snapshot_.voting && (snapshot_.votes[0] < 0 || snapshot_.votes[1] < 0))
                    result[0].placeholder = true;
            }
            break;
        case Screen::Playing:
        case Screen::Settings:
            break;
    }
    const float top = screen_ == Screen::Armory                                          ? 398
                      : screen_ == Screen::Maps                                          ? 270
                      : screen_ == Screen::BotSetup                                      ? 270
                      : screen_ == Screen::BotResult                                     ? 300
                      : screen_ == Screen::Modes                                         ? 166
                      : (screen_ == Screen::Browser || screen_ == Screen::OnlineBrowser) ? 161
                      : screen_ == Screen::Paused                                        ? 214
                      : (screen_ == Screen::Address)                                     ? 276
                      : screen_ == Screen::Lobby || screen_ == Screen::Result ||
                              screen_ == Screen::Connecting || screen_ == Screen::Maps
                          ? 300
                          : 200;
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i].bounds = {
            {264, top + static_cast<float>(i) *
                            ((screen_ == Screen::BotSetup || screen_ == Screen::Maps)
                                 ? 44
                                 : ((screen_ == Screen::Browser || screen_ == Screen::OnlineBrowser ||
                                     screen_ == Screen::Lobby || screen_ == Screen::Maps)
                                        ? 48
                                        : 66))},
            {432, (screen_ == Screen::BotSetup || screen_ == Screen::Maps) ? 38.f
                  : (screen_ == Screen::Browser || screen_ == Screen::OnlineBrowser ||
                     screen_ == Screen::Lobby || screen_ == Screen::Maps)
                      ? 42.f
                      : 52.f}};
    return result;
}

void Menu::open(Screen screen)
{
    screen_ = screen;
    selection_ = 0;
    pressed_.reset();
    message_.clear();
    if (screen == Screen::BotSetup || screen == Screen::Maps || screen == Screen::Main ||
        screen == Screen::Modes || screen == Screen::Lan || screen == Screen::Online)
        bots_ = false;
    if (screen == Screen::Main || screen == Screen::Modes || screen == Screen::Lan ||
        screen == Screen::BotSetup || screen == Screen::Maps)
    {
        lan_ = false;
        online_ = false;
    }
    if (screen == Screen::Online || screen == Screen::OnlineBrowser)
    {
        online_ = true;
        lan_ = false;
    }
    if (screen == Screen::Connecting || screen == Screen::Lobby)
        lan_ = true;
}

void Menu::moveSelection(int direction)
{
    const int count = static_cast<int>(buttons().size());
    if (count)
    {
        selection_ = (selection_ + direction % count + count) % count;
        pressed_.reset();
    }
}

std::optional<int> Menu::buttonAt(Vec2 position) const
{
    const auto items = buttons();
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        const auto& r = items[i].bounds;
        if (position.x >= r.position.x && position.x < r.position.x + r.size.x &&
            position.y >= r.position.y && position.y < r.position.y + r.size.y)
            return static_cast<int>(i);
    }
    return {};
}

void Menu::hover(Vec2 p)
{
    if (auto i = buttonAt(p))
        selection_ = *i;
}

void Menu::press(Vec2 p)
{
    pressed_ = buttonAt(p);
    hover(p);
}

MenuAction Menu::release(Vec2 p)
{
    const auto pressed = pressed_;
    pressed_.reset();
    if (pressed && buttonAt(p) == pressed)
    {
        selection_ = *pressed;
        return activate();
    }
    return MenuAction::None;
}

MenuAction Menu::activate()
{
    const auto items = buttons();
    if (items.empty())
        return MenuAction::None;
    if (items[selection_].placeholder)
    {
        message_ = screen_ == Screen::Armory          ? U"Ожидаем завершения выбора оружия"
                   : screen_ == Screen::OnlineBrowser ? U"Сервер занят или матч уже начался"
                   : screen_ == Screen::Browser       ? U"Комната недоступна. Можно ввести IP"
                   : screen_ == Screen::Modes         ? U"Этот режим пока недоступен"
                                                      : U"Настройки пока недоступны";
    }
    else if (screen_ == Screen::Main)
    {
        if (selection_ == 1)
            return MenuAction::OpenSettings;
        if (selection_ == 2)
            return MenuAction::Quit;
        open(Screen::Modes);
    }
    else if (screen_ == Screen::Modes)
    {
        if (selection_ == 0)
            open(Screen::Online);
        else if (selection_ == 1)
            open(Screen::Lan);
        else if (selection_ == 2)
            open(Screen::BotSetup);
        else if (selection_ == 3)
        {
            open(Screen::Maps);
        }
        else
            open(Screen::Main);
    }
    else if (screen_ == Screen::Paused)
    {
        if (selection_ == 1)
            return MenuAction::OpenSettings;
        if (selection_ == 3)
            return MenuAction::Quit;
        const bool leave = lan_ && selection_ == 2;
        open(selection_ == 0 ? Screen::Playing : Screen::Main);
        if (leave)
            return MenuAction::LeaveRoom;
    }
    else if (screen_ == Screen::Online)
    {
        if (selection_ == 0)
            return MenuAction::QuickMatch;
        if (selection_ == 1)
        {
            open(Screen::OnlineBrowser);
            return MenuAction::FetchOnline;
        }
        open(Screen::Modes);
    }
    else if (screen_ == Screen::OnlineBrowser)
    {
        const int count = static_cast<int>(
            std::max<std::size_t>(1, std::min<std::size_t>(3, onlineRooms_.size() - onlinePage_ * 3)));
        if (selection_ < count && !onlineRooms_.empty())
        {
            selectedServer_ = onlineRooms_[onlinePage_ * 3 + selection_].id;
            return MenuAction::JoinOnline;
        }
        if (onlineRooms_.size() > 3 && selection_ == count)
        {
            onlinePage_ = (onlinePage_ + 1) % ((onlineRooms_.size() + 2) / 3);
            selection_ = 0;
            cancelClick();
        }
        else if (selection_ == count + (onlineRooms_.size() > 3 ? 1 : 0))
            return MenuAction::FetchOnline;
        else
            open(Screen::Online);
    }
    else if (screen_ == Screen::Lan)
    {
        if (selection_ == 0)
        {
            open(Screen::Connecting);
            return MenuAction::CreateRoom;
        }
        if (selection_ == 1)
        {
            open(Screen::Browser);
            return MenuAction::FindRooms;
        }
        open(selection_ == 2 ? Screen::Address : Screen::Modes);
    }
    else if (screen_ == Screen::Browser)
    {
        const int count = static_cast<int>(
            std::max<std::size_t>(1, std::min<std::size_t>(3, rooms_.size() - roomPage_ * 3)));
        if (selection_ < count && !rooms_.empty())
        {
            selectedAddress_ = rooms_[roomPage_ * 3 + selection_].address;
            open(Screen::Connecting);
            return MenuAction::JoinRoom;
        }
        if (rooms_.size() > 3 && selection_ == count)
        {
            roomPage_ = (roomPage_ + 1) % ((rooms_.size() + 2) / 3);
            selection_ = 0;
            cancelClick();
        }
        else
            open(selection_ == count + (rooms_.size() > 3 ? 1 : 0) ? Screen::Address : Screen::Lan);
    }
    else if (screen_ == Screen::Address)
    {
        if (selection_ == 0)
            return MenuAction::JoinAddress;
        open(Screen::Lan);
    }
    else if (screen_ == Screen::Connecting)
        return MenuAction::CancelConnection;
    else if (screen_ == Screen::Maps)
    {
        if (selection_ == 0 || selection_ == 1)
        {
            const int count = static_cast<int>(mapCatalog().size());
            selectedMap_ = static_cast<MapId>(
                (static_cast<int>(selectedMap_) + (selection_ == 0 ? count - 1 : 1)) % count);
        }
        else if (selection_ == 2)
        {
            open(Screen::Playing);
            return MenuAction::StartTest;
        }
        else if (selection_ == 4)
        {
            testWeapon_ = static_cast<WeaponKind>((static_cast<unsigned>(testWeapon_) + 1) % weaponCount);
            testWeaponLabel_ = U"Оружие: " + std::u32string(weaponDefinition(testWeapon_).name);
        }
        else
            open(Screen::Modes);
    }
    else if (screen_ == Screen::Armory)
        return selection_ == 0 ? MenuAction::RollWeapon : MenuAction::KeepWeapon;
    else if (screen_ == Screen::BotSetup)
    {
        if (selection_ <= 1)
        {
            const int count = static_cast<int>(MapId::NatureLarge) + 1;
            botMap_ =
                static_cast<MapId>((static_cast<int>(botMap_) + (selection_ == 0 ? count - 1 : 1)) % count);
        }
        else if (selection_ == 2)
            botDifficulty_ = static_cast<BotDifficulty>((static_cast<int>(botDifficulty_) + 1) % 3);
        else if (selection_ == 3)
        {
            bots_ = true;
            open(Screen::Playing);
            return MenuAction::StartBots;
        }
        else
            open(Screen::Modes);
    }
    else if (screen_ == Screen::BotResult)
    {
        if (selection_ == 0)
        {
            open(Screen::Playing);
            return MenuAction::BotRematch;
        }
        open(selection_ == 1 ? Screen::BotSetup : Screen::Main);
    }
    else if (screen_ == Screen::Lobby || screen_ == Screen::Result)
    {
        if (selection_ == 0)
        {
            if (screen_ == Screen::Lobby && snapshot_.voting && own_ && snapshot_.votes[own_ - 1] < 0)
                return MenuAction::SelectMap;
            return MenuAction::ToggleReady;
        }
        if (screen_ == Screen::Result)
            return selection_ == 1 ? MenuAction::ChangeMap : MenuAction::LeaveRoom;
        if (selection_ == 1 || selection_ == 2)
        {
            int current = snapshot_.voting && own_ && snapshot_.votes[own_ - 1] >= 0
                              ? snapshot_.votes[own_ - 1]
                              : static_cast<int>(snapshot_.map);
            const int count = static_cast<int>(MapId::NatureLarge) + 1;
            selectedMap_ = static_cast<MapId>((current + (selection_ == 1 ? count - 1 : 1)) % count);
            return MenuAction::SelectMap;
        }
        return MenuAction::LeaveRoom;
    }
    return MenuAction::None;
}

void Menu::escape()
{
    if (screen_ == Screen::Playing || screen_ == Screen::Armory)
        open(Screen::Paused);
    else if (screen_ == Screen::Paused)
        open(Screen::Playing);
    else if (screen_ == Screen::Modes)
        open(Screen::Main);
    else if (screen_ == Screen::Maps || screen_ == Screen::BotSetup)
        open(Screen::Modes);
    else if (screen_ == Screen::BotResult)
        open(Screen::Main);
    else if (screen_ == Screen::Lan || screen_ == Screen::Online)
        open(Screen::Modes);
    else if (screen_ == Screen::OnlineBrowser)
        open(Screen::Online);
    else if (screen_ == Screen::Browser || screen_ == Screen::Address)
        open(Screen::Lan);
    // Connection/lobby/result require explicit Cancel/Leave action in Application.
}

void Menu::loseFocus()
{
    cancelClick();
    if (bots_ && (playing() || screen_ == Screen::Armory))
        open(Screen::Paused);
}

void Menu::cancelClick()
{
    pressed_.reset();
}

void Menu::setRooms(std::vector<ListedRoom> rooms)
{
    bool changed = rooms.size() != rooms_.size();
    for (std::size_t i = 0; !changed && i < rooms.size(); ++i)
        changed = rooms[i].address != rooms_[i].address || rooms[i].players != rooms_[i].players ||
                  rooms[i].joinable != rooms_[i].joinable;
    if (!changed)
        return;
    rooms_ = std::move(rooms);
    labels_.clear();
    if (roomPage_ * 3 >= rooms_.size())
        roomPage_ = 0;
    for (const auto& room : rooms_)
        labels_.push_back(asciiText(room.address + "  " + std::to_string(room.players) + "/2") +
                          (room.joinable ? U"" : U" Занята"));
    selection_ = std::min(selection_, static_cast<int>(buttons().size()) - 1);
    cancelClick();
}

void Menu::setOnlineRooms(std::vector<network::DirectoryRoom> rooms)
{
    bool changed = rooms.size() != onlineRooms_.size();
    for (std::size_t i = 0; !changed && i < rooms.size(); ++i)
    {
        const auto& a = rooms[i];
        const auto& b = onlineRooms_[i];
        changed = a.id != b.id || a.name != b.name || a.players != b.players || a.reserved != b.reserved ||
                  a.joinable != b.joinable || a.phase != b.phase;
    }
    if (!changed)
        return;
    onlineRooms_ = std::move(rooms);
    onlineLabels_.clear();
    if (onlinePage_ * 3 >= onlineRooms_.size())
        onlinePage_ = 0;
    for (const auto& room : onlineRooms_)
        onlineLabels_.push_back(asciiText(room.name + "  " + std::to_string(room.players) + "/2") +
                                (room.reserved ? U" Бронь" : U"") + (room.joinable ? U"" : U" Занят"));
    selection_ = std::min(selection_, static_cast<int>(buttons().size()) - 1);
    cancelClick();
}

void Menu::text(std::string_view value)
{
    for (char c : value)
        if (address_.size() < 21 && ((c >= '0' && c <= '9') || c == '.' || c == ':'))
            address_ += c;
}

void Menu::backspace()
{
    auto& text = address_;
    if (text.empty())
        return;
    auto i = text.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
        --i;
    text.resize(i);
}

void Menu::botState(const WorldSnapshot& snapshot)
{
    snapshot_ = snapshot;
    own_ = 1;
    if (snapshot.phase == MatchPhase::Armory && screen_ != Screen::Armory && screen_ != Screen::Paused &&
        screen_ != Screen::Settings)
        open(Screen::Armory);
    else if (snapshot.phase != MatchPhase::Armory && screen_ == Screen::Armory)
        open(Screen::Playing);
    if (snapshot.phase == MatchPhase::Finished && screen_ == Screen::Playing)
        open(Screen::BotResult);
}

void Menu::networkState(const WorldSnapshot& snapshot, PlayerId own)
{
    if (screen_ != Screen::Maps)
        selectedMap_ =
            snapshot.phase == MatchPhase::Lobby && snapshot.voting && own && snapshot.votes[own - 1] >= 0
                ? static_cast<MapId>(snapshot.votes[own - 1])
                : snapshot.map;
    snapshot_ = snapshot;
    own_ = own;
    lan_ = true;
    bots_ = false;
    if (screen_ == Screen::Settings)
        return;
    const auto phase = snapshot.phase;
    if (phase == MatchPhase::Lobby)
    {
        if (screen_ != Screen::Lobby)
            open(Screen::Lobby);
        if (snapshot.aborted != AbortReason::None)
            message_ = U"Игрок отключился. Матч отменён, счёт сброшен";
    }
    else if (phase == MatchPhase::Armory)
    {
        if (screen_ != Screen::Paused && screen_ != Screen::Armory)
            open(Screen::Armory);
    }
    else if (phase == MatchPhase::Finished)
    {
        if (screen_ != Screen::Result)
            open(Screen::Result);
    }
    else if (screen_ != Screen::Playing && screen_ != Screen::Paused)
        open(Screen::Playing);
}
} // namespace cubeshot::client

namespace cubeshot::client
{
void Menu::openSettings()
{
    settingsReturn_ = screen_;
    open(Screen::Settings);
}

void Menu::closeSettings()
{
    if (screen_ != Screen::Settings)
        return;
    if (settingsReturn_ == Screen::Main)
    {
        open(Screen::Main);
        return;
    }
    if (!lan_)
    {
        open(Screen::Paused);
        return;
    }
    if (snapshot_.phase == MatchPhase::Lobby)
        open(Screen::Lobby);
    else if (snapshot_.phase == MatchPhase::Finished)
        open(Screen::Result);
    else
        open(Screen::Paused);
}
} // namespace cubeshot::client
