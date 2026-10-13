#pragma once

#include "game/Types.h"

#include <optional>
#include <string_view>
#include <vector>

namespace cubeshot::client {

enum class Screen { Main, Modes, Playing, Paused };
enum class MenuAction { None, StartTest, Quit };

struct MenuButton {
    std::u32string_view label;
    Rect bounds;
    bool placeholder = false;
};

class Menu {
public:
    Screen screen() const;
    bool playing() const;
    int selection() const;
    std::u32string_view message() const;
    std::vector<MenuButton> buttons() const;

    void moveSelection(int direction);
    void hover(Vec2 position);
    void press(Vec2 position);
    MenuAction release(Vec2 position);
    MenuAction activate();
    void escape();
    void cancelClick();

private:
    std::optional<int> buttonAt(Vec2 position) const;
    void open(Screen screen);

    Screen screen_ = Screen::Main;
    int selection_ = 0;
    std::optional<int> pressed_;
    std::u32string_view message_;
};

} // namespace cubeshot::client
