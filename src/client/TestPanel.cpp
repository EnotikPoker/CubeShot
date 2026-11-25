#include "client/TestPanel.h"
#include "client/AbilityUI.h"
#include "client/UiText.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace cubeshot::client
{
namespace
{
std::u32string number(unsigned n)
{
    auto s = std::to_string(n);
    return {s.begin(), s.end()};
}

bool text(SDL_Renderer* r, std::u32string_view value, float x, float y,
          SDL_Color color = {232, 240, 255, 255})
{
    return drawText(r, value, {x, y}, 1, color);
}
} // namespace

void TestPanel::open(const TestConfiguration& config, bool live)
{
    draft_ = config;
    live_ = live;
    status_.clear();
    state_.clear();
}

std::vector<MenuButton> TestPanel::buttons() const
{
    std::vector<MenuButton> out;
    controls_.clear();
    auto add = [&](std::u32string_view label, Rect box, Kind kind, unsigned value = 0, bool disabled = false)
    {
        auto i = out.size();
        labels_[i] = label;
        out.push_back({labels_[i], box, disabled});
        controls_.push_back({kind, value});
    };
    for (unsigned i = 0; i < 4; ++i)
        add(i ? U"Мишень " + number(i) : U"Игрок", {{30.f + i * 232, 66}, {205, 30}}, Kind::Target, i);
    const std::array<std::u32string_view, 5> sections{U"Способности", U"Оружие", U"Состояние", U"Сцена",
                                                      U"Активные"};
    for (unsigned i = 0; i < 5; ++i)
        add(sections[i], {{30.f + i * 180, 106}, {165, 30}}, Kind::Section, i);
    if (section_ == 0)
    {
        for (unsigned row = 0; row < 6; ++row)
        {
            auto index = page_ * 6 + row;
            if (index >= abilityCount)
                break;
            auto ability = static_cast<AbilityKind>(index);
            add(U"−", {{762, 150.f + row * 35}, {55, 28}}, Kind::Minus, index,
                !draft_.abilities[target_].level(ability));
            add(U"+", {{834, 150.f + row * 35}, {55, 28}}, Kind::Plus, index,
                !draft_.abilities[target_].canAdd(ability));
        }
        add(U"Предыдущая", {{30, 370}, {145, 30}}, Kind::Previous, 0, page_ == 0);
        add(U"Следующая", {{190, 370}, {145, 30}}, Kind::Next, 0, (page_ + 1) * 6 >= abilityCount);
    }
    else if (section_ == 1)
    {
        for (unsigned i = 0; i < weaponCount; ++i)
            add(weaponDefinition(static_cast<WeaponKind>(i)).name,
                {{30.f + (i % 4) * 232, 152.f + (i / 4) * 42}, {213, 34}}, Kind::Weapon, i);
    }
    else if (section_ == 2)
    {
        add(U"HP −10%", {{30, 173}, {205, 32}}, Kind::HealthMinus);
        add(U"HP +10%", {{262, 173}, {205, 32}}, Kind::HealthPlus);
        const std::array<std::pair<std::u32string_view, TestOperation>, 13> actions{
            {{U"Вылечить", TestOperation::Heal},
             {U"Воскресить", TestOperation::Respawn},
             {U"Убрать щиты", TestOperation::EmptyShields},
             {U"Восстановить щиты", TestOperation::RefillShields},
             {U"Опустошить джетпак", TestOperation::EmptyFuel},
             {U"Заправить джетпак", TestOperation::RefillFuel},
             {U"Опустошить магазин", TestOperation::EmptyAmmo},
             {U"Пополнить магазин", TestOperation::RefillAmmo},
             {U"Начать перезарядку", TestOperation::Reload},
             {U"Завершить перезарядку", TestOperation::FinishReload},
             {U"Перегреть миниган", TestOperation::Overheat},
             {U"Охладить оружие", TestOperation::Cool},
             {U"Потушить", TestOperation::Extinguish}}};
        for (unsigned i = 0; i < actions.size(); ++i)
        {
            auto n = i + 2;
            add(actions[i].first, {{30.f + (n % 4) * 232, 173.f + (n / 4) * 42}, {205, 32}}, Kind::Operation,
                static_cast<unsigned>(actions[i].second), !live_);
        }
    }
    else if (section_ == 3)
    {
        add(U"Восстановить карту", {{210, 180}, {540, 38}}, Kind::Operation,
            static_cast<unsigned>(TestOperation::RestoreMap), !live_);
        add(U"Очистить пули, огонь и останки", {{210, 236}, {540, 38}}, Kind::Operation,
            static_cast<unsigned>(TestOperation::Clear), !live_);
        add(U"Перезапустить тест (F5)", {{210, 292}, {540, 38}}, Kind::Restart, 0, !live_);
    }
    if (section_ == 4)
    {
        for (unsigned slot = 0; slot < 2; ++slot)
            add((slot ? U"E · " : U"Q · ") +
                    std::u32string(activeDefinition(draft_.actives[target_][slot]).name),
                {{30.f + slot * 464, 150}, {437, 32}}, Kind::ActiveSlot, slot);
        for (unsigned k = 0; k <= activeCount; ++k)
            add(activeDefinition(ActiveKind(k)).name,
                {{30.f + (k % 4) * 232, 196.f + (k / 4) * 42}, {213, 34}}, Kind::ActiveChoice, k,
                k && ActiveKind(k) == draft_.actives[target_][1 - activeSlot_]);
        add(U"Сбросить перезарядки", {{30, 365}, {285, 32}}, Kind::Operation,
            unsigned(TestOperation::ReadyActives), !live_);
        add(U"Применить Q при продолжении", {{330, 365}, {295, 32}}, Kind::Operation,
            unsigned(TestOperation::UseQ), !live_);
        add(U"Применить E при продолжении", {{640, 365}, {290, 32}}, Kind::Operation,
            unsigned(TestOperation::UseE), !live_);
    }
    add(U"Сбросить персонажа", {{30, 487}, {215, 34}}, Kind::Reset);
    add(U"Сбросить всех", {{260, 487}, {190, 34}}, Kind::ResetAll);
    add(live_ ? U"Вернуться в тест (Tab)" : U"Применить и начать", {{465, 487}, {280, 34}},
        live_ ? Kind::Close : Kind::Apply);
    if (!live_)
        add(U"Отмена", {{760, 487}, {170, 34}}, Kind::Close);
    return out;
}

TestPanel::Intent TestPanel::activate(unsigned index)
{
    auto list = buttons();
    if (index >= list.size() || list[index].placeholder)
        return Intent::None;
    status_.clear();
    const auto c = controls_[index];
    edit_ = {TestOperation::Build, target_};
    switch (c.kind)
    {
        case Kind::ActiveSlot:
            activeSlot_ = c.value;
            return Intent::None;
        case Kind::ActiveChoice:
            draft_.actives[target_][activeSlot_] = ActiveKind(c.value);
            break;
        case Kind::Target:
            target_ = c.value;
            return Intent::None;
        case Kind::Section:
            section_ = c.value;
            return Intent::None;
        case Kind::Minus:
            --draft_.abilities[target_].levels[c.value];
            break;
        case Kind::Plus:
            draft_.abilities[target_].add(static_cast<AbilityKind>(c.value));
            break;
        case Kind::Previous:
            --page_;
            return Intent::None;
        case Kind::Next:
            ++page_;
            return Intent::None;
        case Kind::Weapon:
            draft_.weapons[target_] = static_cast<WeaponKind>(c.value);
            break;
        case Kind::HealthMinus:
            draft_.healthPercent[target_] =
                std::max(1u, draft_.healthPercent[target_] > 10 ? draft_.healthPercent[target_] - 10 : 1u);
            edit_.operation = TestOperation::Health;
            break;
        case Kind::HealthPlus:
            draft_.healthPercent[target_] = std::min(100u, draft_.healthPercent[target_] + 10);
            edit_.operation = TestOperation::Health;
            break;
        case Kind::Operation:
            edit_.operation = static_cast<TestOperation>(c.value);
            break;
        case Kind::Reset:
            draft_.abilities[target_] = {};
            draft_.actives[target_] = {};
            draft_.weapons[target_] = WeaponKind::Pistol;
            draft_.healthPercent[target_] = 100;
            break;
        case Kind::ResetAll:
            draft_ = {};
            edit_.operation = TestOperation::AllBuilds;
            break;
        case Kind::Apply:
            return Intent::Apply;
        case Kind::Close:
            return Intent::Close;
        case Kind::Restart:
            return Intent::Restart;
    }
    return live_ ? Intent::Edit : Intent::None;
}

bool TestPanel::render(SDL_Renderer* r, int selection) const
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 11, 17, 26, 240);
    SDL_FRect bg{0, 0, 960, 540};
    bool ok = SDL_RenderFillRect(r, &bg);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    ok = ok && drawText(r, live_ ? U"Тест · пауза" : U"Настройка теста", {30, 22}, 2, {232, 240, 255, 255});
    auto list = buttons();
    for (unsigned i = 0; i < list.size(); ++i)
    {
        auto c = controls_[i];
        bool active = (c.kind == Kind::Target && c.value == target_) ||
                      (c.kind == Kind::Section && c.value == section_) ||
                      (c.kind == Kind::Weapon && c.value == static_cast<unsigned>(draft_.weapons[target_]));
        active =
            active || (c.kind == Kind::ActiveSlot && c.value == activeSlot_) ||
            (c.kind == Kind::ActiveChoice && ActiveKind(c.value) == draft_.actives[target_][activeSlot_]);
        auto b = list[i].bounds;
        SDL_FRect rect{b.position.x, b.position.y, b.size.x, b.size.y};
        SDL_SetRenderDrawColor(r, active || static_cast<int>(i) == selection ? 45 : 25, active ? 86 : 36,
                               active ? 115 : 50, 255);
        ok = ok && SDL_RenderFillRect(r, &rect) &&
             text(r, list[i].label, b.position.x + 10, b.position.y + 8,
                  list[i].placeholder ? SDL_Color{83, 98, 118, 255} : SDL_Color{232, 240, 255, 255});
    }
    if (section_ == 0)
    {
        for (unsigned row = 0; row < 6; ++row)
        {
            unsigned i = page_ * 6 + row;
            if (i >= abilityCount)
                break;
            auto kind = static_cast<AbilityKind>(i);
            float y = 150 + row * 35.f;
            ok = ok && drawAbilityIcon(r, kind, {{30, y}, {26, 26}}) &&
                 text(r, abilityDefinition(kind).name, 70, y + 5) &&
                 text(r, number(draft_.abilities[target_].level(kind)), 700, y + 5, {100, 195, 235, 255});
        }
        ok = ok && text(r, number(page_ + 1) + U" / " + number((abilityCount + 5) / 6), 370, 379);
        if (selection >= 0 && static_cast<unsigned>(selection) < controls_.size())
        {
            auto c = controls_[selection];
            if (c.kind == Kind::Minus || c.kind == Kind::Plus)
                ok = ok && text(r, abilityDefinition(static_cast<AbilityKind>(c.value)).description, 30, 421,
                                {161, 180, 202, 255});
        }
    }
    if (section_ == 2)
    {
        const auto split = state_.find(U'\n');
        ok = ok && text(r, U"Начальное HP: " + number(draft_.healthPercent[target_]) + U"%", 30, 147) &&
             text(r, state_.substr(0, split), 30, 358, {116, 197, 232, 255});
        if (split != std::u32string::npos)
            ok = ok && text(r, state_.substr(split + 1), 30, 382, {116, 197, 232, 255});
    }
    if (section_ == 3)
        ok = ok && text(r, U"Карта восстанавливается с безопасным возвратом персонажей на старт.", 60, 360);
    if (section_ == 4)
        ok = ok && text(r, activeState_, 30, 397, {116, 197, 232, 255});
    if (section_ == 4 && selection >= 0 && unsigned(selection) < controls_.size() &&
        controls_[selection].kind == Kind::ActiveChoice)
        ok = ok && text(r, activeDefinition(ActiveKind(controls_[selection].value)).description, 30, 420,
                        {161, 180, 202, 255});
    if (!status_.empty())
        ok = ok && text(r, status_, 30, 458, {245, 177, 105, 255});
    return ok;
}
} // namespace cubeshot::client
