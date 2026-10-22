#pragma once
#include "client/Settings.h"
#include "client/WindowSettings.h"
#include "game/Types.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

union SDL_Event;
struct SDL_Window;
struct SDL_Renderer;

namespace cubeshot::client
{
enum class SettingsTab
{
    Player,
    Display,
    Controls,
    Effects
};
enum class SettingsIntent
{
    None,
    Apply,
    Close,
    Confirm,
    Revert
};

struct SettingsItem
{
    enum class Kind
    {
        Tab,
        Nickname,
        DisplayValue,
        EffectValue,
        Binding,
        ClearBinding,
        Apply,
        Cancel,
        Reset,
        Confirm,
        Revert
    };
    Kind kind;
    int value = 0, slot = 0;
    Rect bounds;
    std::u32string label;
    bool enabled = true;
};

class SettingsController
{
public:
    using Save = std::function<bool(const Settings&, const std::string&, std::string&)>;
    void initialize(Settings settings, std::string nickname);
    void begin();

    const Settings& active() const
    {
        return active_;
    }

    const Settings& draft() const
    {
        return draft_;
    }

    const std::string& nickname() const
    {
        return nickname_;
    }

    const std::string& draftNickname() const
    {
        return draftNickname_;
    }

    SettingsTab tab() const
    {
        return tab_;
    }

    void selectTab(SettingsTab tab);

    bool editingNickname() const
    {
        return editingNickname_;
    }

    bool capturing() const
    {
        return capture_.has_value();
    }

    bool previewing() const
    {
        return preview_;
    }

    std::uint64_t remainingSeconds(std::uint64_t now) const;

    const std::u32string& message() const
    {
        return message_;
    }

    int selection() const
    {
        return selection_;
    }

    void setAvailableSizes(std::vector<WindowSize> sizes)
    {
        sizes_ = std::move(sizes);
    }

    std::vector<SettingsItem> items() const;
    SettingsIntent event(const SDL_Event& event, SDL_Window* window, SDL_Renderer* renderer);
    void apply(WindowSettings& window, const Save& save, std::uint64_t now);
    void confirm(WindowSettings& window, const Save& save);
    void cancelPreview(WindowSettings& window);
    void cancel(WindowSettings& window);
    void tick(WindowSettings& window, std::uint64_t now);
    void resetDraft();
    void appendNickname(std::string_view text);
    void backspaceNickname();

    void clearNickname()
    {
        draftNickname_.clear();
    }

    std::u32string bind(InputAction action, unsigned slot, InputBinding value);

private:
    SettingsIntent activate(int index, int direction = 1);
    void finish(const Save& save);
    void restore(WindowSettings& window);
    Settings active_, draft_;
    std::string nickname_ = "Игрок", draftNickname_ = "Игрок", editBefore_;
    SettingsTab tab_ = SettingsTab::Player;
    std::u32string message_;
    std::vector<WindowSize> sizes_{windowSizes.begin(), windowSizes.end()};
    std::optional<std::pair<InputAction, unsigned>> capture_;
    std::optional<int> pressed_;
    int selection_ = 0;
    bool editingNickname_ = false, preview_ = false;
    std::uint64_t deadline_ = 0;
};
} // namespace cubeshot::client
