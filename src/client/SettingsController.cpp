#include "client/SettingsController.h"
#include "network/Identity.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace cubeshot::client
{
namespace
{
std::u32string number(int value)
{
    const auto text = std::to_string(value);
    return {text.begin(), text.end()};
}

bool inside(Vec2 point, const Rect& bounds)
{
    return point.x >= bounds.position.x && point.x < bounds.position.x + bounds.size.x &&
           point.y >= bounds.position.y && point.y < bounds.position.y + bounds.size.y;
}
} // namespace

void SettingsController::initialize(Settings settings, std::string nickname)
{
    active_ = std::move(settings);
    nickname_ = std::move(nickname);
    begin();
}

void SettingsController::begin()
{
    draft_ = active_;
    draftNickname_ = nickname_;
    tab_ = SettingsTab::Player;
    selection_ = 3;
    editingNickname_ = preview_ = false;
    capture_.reset();
    pressed_.reset();
    message_.clear();
}

void SettingsController::selectTab(SettingsTab tab)
{
    tab_ = tab;
    selection_ = 3;
    pressed_.reset();
    capture_.reset();
    message_.clear();
}

std::uint64_t SettingsController::remainingSeconds(std::uint64_t now) const
{
    return preview_ && now < deadline_ ? (deadline_ - now + 999) / 1000 : 0;
}

void SettingsController::appendNickname(std::string_view text)
{
    // Validate each UTF-8 codepoint so a pasted long name is truncated, not wholly discarded.
    for (std::size_t i = 0; i < text.size();)
    {
        std::size_t end = i + 1;
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
            ++end;
        const auto candidate = draftNickname_ + std::string(text.substr(i, end - i));
        if (network::validNickname(candidate))
            draftNickname_ = candidate;
        i = end;
    }
}

void SettingsController::backspaceNickname()
{
    if (draftNickname_.empty())
        return;
    auto i = draftNickname_.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(draftNickname_[i]) & 0xc0) == 0x80)
        --i;
    draftNickname_.resize(i);
}

std::u32string SettingsController::bind(InputAction action, unsigned slot, InputBinding value)
{
    return message_ = assignBinding(draft_.bindings, action, slot, value);
}

void SettingsController::resetDraft()
{
    draft_ = Settings{};
    message_ = U"Экран и управление сброшены. Нажмите Применить";
}

std::vector<SettingsItem> SettingsController::items() const
{
    using Kind = SettingsItem::Kind;
    if (preview_)
        return {{Kind::Confirm, 0, 0, {{170, 280}, {290, 52}}, U"Сохранить"},
                {Kind::Revert, 0, 0, {{500, 280}, {290, 52}}, U"Вернуть"}};
    std::vector<SettingsItem> out;
    const std::u32string tabs[]{U"Игрок", U"Экран", U"Управление"};
    for (int i = 0; i < 3; ++i)
        out.push_back({Kind::Tab, i, 0, {{70.f + 280 * i, 78}, {260, 42}}, tabs[i]});
    if (tab_ == SettingsTab::Player)
        out.push_back({Kind::Nickname,
                       0,
                       0,
                       {{200, 175}, {560, 54}},
                       network::nicknameText(draftNickname_) + (editingNickname_ ? U"_" : U"")});
    if (tab_ == SettingsTab::Display)
    {
        const auto& d = draft_.display;
        const std::u32string labels[]{
            U"Режим: " + std::u32string(d.fullscreen ? U"Полный экран" : U"Окно"),
            U"Размер окна: " + number(d.size.width) + U" x " + number(d.size.height),
            U"VSync: " + std::u32string(d.vsync ? U"Вкл" : U"Выкл"),
            U"Лимит FPS: " + (d.fps ? number(d.fps) : std::u32string(U"Без лимита"))};
        for (int i = 0; i < 4; ++i)
            out.push_back({Kind::DisplayValue,
                           i,
                           0,
                           {{200, 145.f + 58 * i}, {560, 46}},
                           labels[i],
                           i != 1 || !d.fullscreen});
    }
    if (tab_ == SettingsTab::Controls)
        for (int a = 0; a < 5; ++a)
            for (int slot = 0; slot < 2; ++slot)
            {
                const auto binding = draft_.bindings[a][slot];
                const bool capturing = capture_ && capture_->first == static_cast<InputAction>(a) &&
                                       capture_->second == static_cast<unsigned>(slot);
                out.push_back({Kind::Binding,
                               a,
                               slot,
                               {{350.f + slot * 260, 142.f + a * 48}, {192, 36}},
                               capturing ? U"Нажмите..." : bindingName(binding)});
                out.push_back(
                    {Kind::ClearBinding, a, slot, {{548.f + slot * 260, 142.f + a * 48}, {42, 36}}, U"X"});
            }
    out.push_back({Kind::Apply, 0, 0, {{40, 434}, {270, 42}}, U"Применить"});
    out.push_back({Kind::Cancel, 0, 0, {{345, 434}, {270, 42}}, U"Отмена"});
    out.push_back({Kind::Reset, 0, 0, {{650, 434}, {270, 42}}, U"Сбросить настройки"});
    return out;
}

SettingsIntent SettingsController::activate(int index, int direction)
{
    const auto list = items();
    if (index < 0 || index >= static_cast<int>(list.size()) || !list[index].enabled)
        return SettingsIntent::None;
    const auto& item = list[index];
    message_.clear();
    using Kind = SettingsItem::Kind;
    switch (item.kind)
    {
        case Kind::Tab:
            selectTab(static_cast<SettingsTab>(item.value));
            break;
        case Kind::Nickname:
            editingNickname_ = true;
            editBefore_ = draftNickname_;
            break;
        case Kind::Binding:
            capture_ = {static_cast<InputAction>(item.value), static_cast<unsigned>(item.slot)};
            break;
        case Kind::ClearBinding:
            bind(static_cast<InputAction>(item.value), item.slot, {});
            break;
        case Kind::DisplayValue:
        {
            auto& d = draft_.display;
            if (item.value == 0)
                d.fullscreen = !d.fullscreen;
            if (item.value == 1 && !sizes_.empty())
            {
                auto found = std::find(sizes_.begin(), sizes_.end(), d.size);
                const int current = found == sizes_.end() ? -1 : static_cast<int>(found - sizes_.begin());
                const int count = static_cast<int>(sizes_.size());
                d.size = sizes_[(current + direction + count) % count];
            }
            if (item.value == 2)
                d.vsync = !d.vsync;
            if (item.value == 3)
            {
                const auto found = std::find(frameLimits.begin(), frameLimits.end(), d.fps);
                const int current = static_cast<int>(found - frameLimits.begin());
                d.fps = frameLimits[(current + direction + static_cast<int>(frameLimits.size())) %
                                    frameLimits.size()];
            }
            break;
        }
        case Kind::Apply:
            return SettingsIntent::Apply;
        case Kind::Cancel:
            return SettingsIntent::Close;
        case Kind::Reset:
            resetDraft();
            break;
        case Kind::Confirm:
            return SettingsIntent::Confirm;
        case Kind::Revert:
            return SettingsIntent::Revert;
    }
    return SettingsIntent::None;
}

SettingsIntent SettingsController::event(const SDL_Event& event, SDL_Window* window, SDL_Renderer* renderer)
{
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
    {
        pressed_.reset();
        return SettingsIntent::None;
    }
    const bool keyDown = event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat;
    if (editingNickname_)
    {
        if (event.type == SDL_EVENT_TEXT_INPUT)
            appendNickname(event.text.text);
        if (keyDown)
        {
            const auto key = event.key.scancode;
            if (key == SDL_SCANCODE_ESCAPE)
            {
                draftNickname_ = editBefore_;
                editingNickname_ = false;
            }
            else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)
                editingNickname_ = false;
            else if (key == SDL_SCANCODE_BACKSPACE)
                backspaceNickname();
            else if (key == SDL_SCANCODE_A && (event.key.mod & SDL_KMOD_CTRL))
                clearNickname();
            else if (key == SDL_SCANCODE_V && (event.key.mod & SDL_KMOD_CTRL))
            {
                char* text = SDL_GetClipboardText();
                if (text)
                {
                    appendNickname(text);
                    SDL_free(text);
                }
            }
        }
        return SettingsIntent::None;
    }
    if (capture_)
    {
        if (keyDown && event.key.scancode == SDL_SCANCODE_ESCAPE)
        {
            capture_.reset();
            message_.clear();
        }
        else if (keyDown || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            const InputBinding binding = keyDown ? InputBinding{BindingDevice::Keyboard, event.key.scancode}
                                                 : InputBinding{BindingDevice::Mouse, event.button.button};
            if (bind(capture_->first, capture_->second, binding).empty())
                capture_.reset();
        }
        return SettingsIntent::None;
    }
    if (keyDown)
    {
        const auto key = event.key.scancode;
        if (key == SDL_SCANCODE_ESCAPE)
            return preview_ ? SettingsIntent::Revert : SettingsIntent::Close;
        if (key == SDL_SCANCODE_TAB && !preview_)
            selectTab(static_cast<SettingsTab>(
                (static_cast<int>(tab_) + ((event.key.mod & SDL_KMOD_SHIFT) ? 2 : 1)) % 3));
        else if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_DOWN)
        {
            const int count = static_cast<int>(items().size());
            selection_ = (selection_ + (key == SDL_SCANCODE_UP ? -1 : 1) + count) % count;
            pressed_.reset();
        }
        else if (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_RIGHT)
        {
            if (preview_)
                selection_ = 1 - selection_;
            else if (items().at(selection_).kind == SettingsItem::Kind::DisplayValue)
                return activate(selection_, key == SDL_SCANCODE_LEFT ? -1 : 1);
            else
            {
                const int count = static_cast<int>(items().size());
                selection_ = (selection_ + (key == SDL_SCANCODE_LEFT ? -1 : 1) + count) % count;
            }
        }
        else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)
            return activate(selection_);
    }
    const bool click =
        (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.button == SDL_BUTTON_LEFT;
    if (click || event.type == SDL_EVENT_MOUSE_MOTION)
    {
        Vec2 point;
        const float x = click ? event.button.x : event.motion.x;
        const float y = click ? event.button.y : event.motion.y;
        if (!SDL_RenderCoordinatesFromWindow(renderer, x, y, &point.x, &point.y))
            return SettingsIntent::None;
        const auto list = items();
        std::optional<int> hit;
        for (std::size_t i = 0; i < list.size(); ++i)
            if (inside(point, list[i].bounds))
                hit = static_cast<int>(i);
        if (hit)
            selection_ = *hit;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            pressed_ = hit;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            const auto pressed = pressed_;
            pressed_.reset();
            if (pressed && hit == pressed)
                return activate(*hit);
        }
    }
    (void)window;
    return SettingsIntent::None;
}

void SettingsController::finish(const Save& save)
{
    active_ = draft_;
    if (!network::validNickname(draftNickname_))
        draftNickname_ = "Игрок";
    nickname_ = draftNickname_;
    std::string error;
    if (!save(active_, nickname_, error))
    {
        SDL_Log("Settings save failed: %s", error.c_str());
        message_ = U"Не сохранено на диск. Повторите Применить";
    }
    else
        message_ = U"Сохранено. Новый ник — при следующем подключении";
}

void SettingsController::apply(WindowSettings& window, const Save& save, std::uint64_t now)
{
    if (!validDisplay(draft_.display) || !validBindings(draft_.bindings))
    {
        message_ = U"Проверьте настройки";
        return;
    }
    std::string error;
    if (!window.apply(draft_.display, error))
    {
        SDL_Log("Display settings failed: %s", error.c_str());
        restore(window);
        message_ = U"Не удалось применить экран. Прежние параметры возвращены";
        return;
    }
    if (active_.display.fullscreen != draft_.display.fullscreen ||
        active_.display.size != draft_.display.size)
    {
        preview_ = true;
        deadline_ = now + 15000;
        selection_ = 1; // A stray Enter must not confirm a display change.
        pressed_.reset();
    }
    else
        finish(save);
}

void SettingsController::confirm(WindowSettings& window, const Save& save)
{
    if (!preview_)
        return;
    if (!window.settled(draft_.display))
    {
        message_ = U"Ожидание переключения окна";
        return;
    }
    preview_ = false;
    selection_ = 3;
    finish(save);
}

void SettingsController::restore(WindowSettings& window)
{
    std::string error;
    if (!window.apply(active_.display, error))
    {
        active_.display = DisplaySettings{};
        window.apply(active_.display, error);
        SDL_Log("Display rollback fallback: %s", error.c_str());
    }
}

void SettingsController::cancelPreview(WindowSettings& window)
{
    if (!preview_)
        return;
    restore(window);
    preview_ = false;
    selection_ = 3;
    pressed_.reset();
    message_ = U"Изменения экрана отменены";
}

void SettingsController::cancel(WindowSettings& window)
{
    cancelPreview(window);
    begin();
}

void SettingsController::tick(WindowSettings& window, std::uint64_t now)
{
    if (preview_ && now >= deadline_)
        cancelPreview(window);
}
} // namespace cubeshot::client
