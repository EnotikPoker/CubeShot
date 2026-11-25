#pragma once
#include "client/MenuButton.h"
#include "game/TestSession.h"
#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>
struct SDL_Renderer;

namespace cubeshot::client
{
class TestPanel
{
public:
    enum class Intent
    {
        None,
        Edit,
        Apply,
        Close,
        Restart
    };
    void open(const TestConfiguration& config, bool live);
    std::vector<MenuButton> buttons() const;
    Intent activate(unsigned index);
    bool render(SDL_Renderer* renderer, int selection) const;

    const TestConfiguration& draft() const
    {
        return draft_;
    }

    void restore(const TestConfiguration& value)
    {
        draft_ = value;
    }

    const TestEdit& edit() const
    {
        return edit_;
    }

    unsigned target() const
    {
        return target_;
    }

    unsigned page() const
    {
        return page_;
    }

    bool live() const
    {
        return live_;
    }

    void status(std::u32string value)
    {
        status_ = std::move(value);
    }

    void state(std::u32string value)
    {
        state_ = std::move(value);
    }

private:
    enum class Kind
    {
        Target,
        Section,
        Minus,
        Plus,
        Previous,
        Next,
        Weapon,
        HealthMinus,
        HealthPlus,
        Operation,
        Reset,
        ResetAll,
        Apply,
        Close,
        Restart
    };

    struct Control
    {
        Kind kind;
        unsigned value;
    };

    TestConfiguration draft_;
    bool live_ = false;
    unsigned target_ = 0, page_ = 0, section_ = 0;
    TestEdit edit_;
    std::u32string status_, state_;
    mutable std::array<std::u32string, 80> labels_;
    mutable std::vector<Control> controls_;
};
} // namespace cubeshot::client
