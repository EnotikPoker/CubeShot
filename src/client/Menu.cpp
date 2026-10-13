#include "client/Menu.h"

namespace cubeshot::client {

Screen Menu::screen() const { return screen_; }
bool Menu::playing() const { return screen_ == Screen::Playing; }
int Menu::selection() const { return selection_; }
std::u32string_view Menu::message() const { return message_; }

std::vector<MenuButton> Menu::buttons() const
{
    std::vector<MenuButton> result;
    if (screen_ == Screen::Main) {
        result = {{U"Играть", {}}, {U"Настройки", {}, true}, {U"Выйти из игры", {}}};
    } else if (screen_ == Screen::Modes) {
        result = {{U"Мультиплеер", {}, true}, {U"Локальная игра", {}, true},
                  {U"Игра с ботами", {}, true}, {U"Тест", {}}, {U"Назад", {}}};
    } else if (screen_ == Screen::Paused) {
        result = {{U"Продолжить", {}}, {U"Настройки", {}, true},
                  {U"Выйти в главное меню", {}}, {U"Выйти из игры", {}}};
    }
    const float top = screen_ == Screen::Modes ? 166.0f :
                      screen_ == Screen::Paused ? 214.0f : 200.0f;
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i].bounds = {{264, top + static_cast<float>(i) * 66}, {432, 52}};
    }
    return result;
}

void Menu::open(Screen screen)
{
    screen_ = screen;
    selection_ = 0;
    pressed_.reset();
    message_ = {};
}

void Menu::moveSelection(int direction)
{
    const int count = static_cast<int>(buttons().size());
    if (count != 0) {
        selection_ = (selection_ + direction % count + count) % count;
        pressed_.reset();
    }
}

std::optional<int> Menu::buttonAt(Vec2 position) const
{
    const auto items = buttons();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Rect& rect = items[i].bounds;
        if (position.x >= rect.position.x && position.x < rect.position.x + rect.size.x &&
            position.y >= rect.position.y && position.y < rect.position.y + rect.size.y) {
            return static_cast<int>(i);
        }
    }
    return std::nullopt;
}

void Menu::hover(Vec2 position)
{
    if (const auto index = buttonAt(position)) {
        selection_ = *index;
    }
}

void Menu::press(Vec2 position)
{
    pressed_ = buttonAt(position);
    hover(position);
}

MenuAction Menu::release(Vec2 position)
{
    const auto pressed = pressed_;
    pressed_.reset();
    if (pressed && buttonAt(position) == pressed) {
        selection_ = *pressed;
        return activate();
    }
    return MenuAction::None;
}

MenuAction Menu::activate()
{
    const auto items = buttons();
    if (items.empty()) {
        return MenuAction::None;
    }
    if (items[selection_].placeholder) {
        message_ = screen_ == Screen::Modes ? U"Этот режим пока недоступен" :
                                             U"Настройки пока недоступны";
    } else if (screen_ == Screen::Main) {
        if (selection_ == 2) return MenuAction::Quit;
        open(Screen::Modes);
    } else if (screen_ == Screen::Modes) {
        if (selection_ == 3) {
            open(Screen::Playing);
            return MenuAction::StartTest;
        }
        open(Screen::Main);
    } else if (screen_ == Screen::Paused) {
        if (selection_ == 3) return MenuAction::Quit;
        open(selection_ == 0 ? Screen::Playing : Screen::Main);
    }
    return MenuAction::None;
}

void Menu::escape()
{
    if (screen_ == Screen::Playing) open(Screen::Paused);
    else if (screen_ == Screen::Paused) open(Screen::Playing);
    else if (screen_ == Screen::Modes) open(Screen::Main);
}

void Menu::cancelClick() { pressed_.reset(); }

} // namespace cubeshot::client
