#pragma once
#include "client/MenuButton.h"
#include "client/TestPanel.h"
#include "game/BotController.h"
#include "game/Match.h"
#include "network/Directory.h"
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cubeshot::client
{
enum class Screen
{
    Main,
    Modes,
    Playing,
    Paused,
    Lan,
    Browser,
    Address,
    Connecting,
    Lobby,
    Result,
    Online,
    OnlineBrowser,
    Settings,
    Maps,
    BotSetup,
    BotResult,
    Armory,
    TestSetup
};
enum class MenuAction
{
    None,
    SelectRules,
    ApplyTest,
    EditTest,
    StartTest,
    StartBots,
    BotRematch,
    RollWeapon,
    RollAbility,
    RollActiveQ,
    RollActiveE,
    KeepWeapon,
    Quit,
    CreateRoom,
    FindRooms,
    JoinAddress,
    JoinRoom,
    CancelConnection,
    ToggleReady,
    LeaveRoom,
    QuickMatch,
    FetchOnline,
    JoinOnline,
    OpenSettings,
    SelectMap,
    ChangeMap
};

struct ListedRoom
{
    std::string address;
    unsigned players;
    bool joinable;
};

class Menu
{
public:
    MatchRules matchRules() const
    {
        return {wins_, gameMode_, stages_, controlGoal_};
    }

    unsigned wins() const
    {
        return wins_;
    }

    using TestConfiguration = cubeshot::TestConfiguration;

    const TestConfiguration& testConfiguration() const
    {
        return test_;
    }

    const TestConfiguration& testDraft() const
    {
        return testPanel_.draft();
    }

    unsigned testTarget() const
    {
        return testPanel_.target();
    }

    unsigned testPage() const
    {
        return testPanel_.page();
    }

    const TestPanel& testPanel() const
    {
        return testPanel_;
    }

    bool liveTestPanel() const
    {
        return screen_ == Screen::TestSetup && testPanel_.live();
    }

    void openTestPanel(bool live, Screen returnScreen);
    void completeTestEdit(bool success);

    void testActiveStatus(std::u32string text)
    {
        testPanel_.activeState(std::move(text));
    }

    void testStatus(std::u32string value)
    {
        testPanel_.state(std::move(value));
    }

    bool bots() const
    {
        return bots_;
    }

    MapId botMap() const
    {
        return botMap_;
    }

    BotDifficulty botDifficulty() const
    {
        return botDifficulty_;
    }

    void botState(const WorldSnapshot& snapshot);

    WeaponKind testWeapon() const
    {
        return testWeapon_;
    }

    MapId selectedMap() const
    {
        return selectedMap_;
    }

    void setSelectedMap(MapId id)
    {
        selectedMap_ = id;
    }

    Screen screen() const;
    bool playing() const;

    bool lan() const
    {
        return lan_;
    }

    bool online() const
    {
        return online_;
    }

    const std::string& nickname() const
    {
        return nickname_;
    }

    void setNickname(std::string name)
    {
        nickname_ = std::move(name);
    }

    void setOnlineRooms(std::vector<network::DirectoryRoom> rooms);

    const std::string& selectedServer() const
    {
        return selectedServer_;
    }

    int selection() const;
    std::u32string_view message() const;
    std::vector<MenuButton> buttons() const;
    void moveSelection(int direction);
    void hover(Vec2 position);
    void press(Vec2 position);
    MenuAction release(Vec2 position);
    MenuAction activate();
    void escape();
    void loseFocus();
    void openSettings();
    void closeSettings();
    void cancelClick();
    void open(Screen screen);

    void setMessage(std::u32string message)
    {
        message_ = std::move(message);
    }

    void setRooms(std::vector<ListedRoom> rooms);

    const std::string& selectedAddress() const
    {
        return selectedAddress_;
    }

    const std::string& address() const
    {
        return address_;
    }

    void text(std::string_view value);
    void backspace();
    void networkState(const WorldSnapshot& snapshot, PlayerId own);

    const WorldSnapshot& networkSnapshot() const
    {
        return snapshot_;
    }

    PlayerId ownId() const
    {
        return own_;
    }

private:
    unsigned wins_ = 15, stages_ = 14, controlGoal_ = 180;
    GameMode gameMode_ = GameMode::Duel;
    std::u32string modeLabel_ = U"Режим: Дуэль";
    void updateRuleLabels();
    void cycleTarget();
    TestConfiguration test_;
    TestPanel testPanel_;
    Screen testReturn_ = Screen::Maps;
    std::u32string winsLabel_ = U"До 15 побед";
    std::optional<int> buttonAt(Vec2 position) const;
    MapId selectedMap_ = MapId::Test;
    WeaponKind testWeapon_ = WeaponKind::Pistol;
    std::u32string testWeaponLabel_ = U"Оружие: Пистолет";
    MapId botMap_ = MapId::Duel;
    BotDifficulty botDifficulty_ = BotDifficulty::Medium;
    bool bots_ = false;
    Screen screen_ = Screen::Main;
    Screen settingsReturn_ = Screen::Main;
    int selection_ = 0;
    std::optional<int> pressed_;
    std::u32string message_;
    bool lan_ = false, online_ = false;
    std::string nickname_ = "Игрок", selectedServer_;
    std::vector<network::DirectoryRoom> onlineRooms_;
    std::vector<std::u32string> onlineLabels_;
    std::size_t onlinePage_ = 0;
    std::string address_, selectedAddress_;
    std::vector<ListedRoom> rooms_;
    std::size_t roomPage_ = 0;
    std::vector<std::u32string> labels_;
    WorldSnapshot snapshot_;
    PlayerId own_ = 0;
};

std::u32string asciiText(std::string_view text);
} // namespace cubeshot::client
