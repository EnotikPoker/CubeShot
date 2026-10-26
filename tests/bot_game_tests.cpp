#include "client/BotGameController.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;
using namespace cubeshot::client;

namespace
{
void expect(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void select(Menu& menu, int item)
{
    menu.moveSelection(item - menu.selection());
}

void test()
{
    Menu menu;
    BotGameController controller;
    menu.activate();
    select(menu, 2);
    menu.activate();
    expect(menu.screen() == Screen::BotSetup && !menu.bots() && !menu.lan() && !menu.online(),
           "Bot setup is independent from network and test modes");
    expect(menu.botMap() == MapId::Duel && menu.botDifficulty() == BotDifficulty::Medium,
           "Defaults are classic duel and medium difficulty");
    for (int i = 0; i < 10; ++i)
    {
        select(menu, 1);
        menu.activate();
        expect(validMap(menu.botMap()) && menu.botMap() != MapId::Test, "All bot maps are duel maps");
    }
    expect(menu.botMap() == MapId::Duel && menu.selectedMap() == MapId::Test,
           "Map cycling wraps independently of test selection");
    select(menu, 2);
    menu.activate();
    expect(menu.botDifficulty() == BotDifficulty::Hard, "Difficulty can be changed");
    menu.escape();
    menu.moveSelection(2);
    menu.activate();
    expect(menu.botDifficulty() == BotDifficulty::Hard, "Choice survives leaving setup");
    select(menu, 3);
    auto button = menu.buttons()[3].bounds;
    Vec2 click{button.position.x + 10, button.position.y + 10};
    menu.press(click);
    auto action = menu.release(click);
    expect(action == MenuAction::StartBots && menu.playing() && menu.bots(), "Complete click starts match");
    controller.handle(action, menu, "Тест игрока");
    expect(controller.match() && controller.match()->snapshot().names[0] == "Тест игрока",
           "Controller creates local match using nickname");
    for (int i = 0; i < 370; ++i)
        controller.update(1.f / 120, menu);
    expect(controller.match()->snapshot().phase == MatchPhase::Round, "Countdown advances normally");
    controller.action(false, true, {1, 0});
    menu.loseFocus();
    controller.clearInput();
    const auto paused = controller.match()->snapshot();
    for (int i = 0; i < 600; ++i)
        controller.update(1.f / 120, menu);
    const auto after = controller.match()->snapshot();
    expect(menu.screen() == Screen::Paused && after.tick == paused.tick &&
               after.players[1].position.x == paused.players[1].position.x &&
               after.remaining == paused.remaining,
           "Focus loss freezes bot, physics and timers");
    menu.openSettings();
    controller.update(10, menu);
    expect(controller.match()->snapshot().tick == paused.tick, "Settings do not advance simulation");
    menu.closeSettings();
    expect(menu.screen() == Screen::Paused, "Settings return to paused match");
    menu.escape();
    controller.update(1.f / 120, menu);
    for (auto event : controller.match()->takeEvents())
        expect(!(event.kind == EventKind::Shot && event.actor == 1), "Paused pending shot is cancelled");
    auto result = controller.match()->snapshot();
    result.phase = MatchPhase::Finished;
    result.winner = 2;
    result.score[1] = 5;
    menu.botState(result);
    expect(menu.screen() == Screen::BotResult && !menu.lan() && menu.ownId() == 1,
           "Autonomous result does not enter network flow");
    select(menu, 1);
    menu.activate();
    controller.synchronize(menu);
    expect(menu.screen() == Screen::BotSetup && !controller.match(), "Changing options destroys old match");
    select(menu, 3);
    controller.handle(menu.activate(), menu, "Новый ник");
    expect(controller.match()->snapshot().names[0] == "Новый ник", "New match applies changed nickname");
    menu.escape();
    select(menu, 2);
    menu.activate();
    controller.synchronize(menu);
    expect(menu.screen() == Screen::Main && !controller.match() && !menu.bots(), "Exit releases match");
    menu.open(Screen::Maps);
    select(menu, 2);
    expect(menu.activate() == MenuAction::StartTest && !menu.bots(), "Test still starts target-only world");
    Match lan;
    lan.connect(1);
    lan.connect(2);
    lan.setReady(1, true);
    lan.setReady(2, true);
    menu.networkState(lan.snapshot(), 1);
    menu.loseFocus();
    expect(menu.lan() && menu.playing() && !menu.bots(), "LAN focus policy remains unchanged");
}
} // namespace

int main()
{
    try
    {
        test();
        std::cout << "Bot menus, pause and lifetime passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
