#pragma once

#include "client/BotGameController.h"
#include "client/ConnectionController.h"
#include "client/Effects.h"
#include "client/Input.h"
#include "client/Menu.h"
#include "client/Profile.h"
#include "client/SettingsController.h"
#include "game/World.h"
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Cursor;

namespace cubeshot::client
{

class Application
{
public:
    explicit Application(std::string directoryUrl = {}) : directoryOverride_(std::move(directoryUrl)) {}

    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();
    int smokeTest();

private:
    bool initialize();
    Player ownPlayer() const;
    Rect arenaBounds() const;
    PlayerInput rawInput() const;
    bool handle(MenuAction action);
    void screenChanged(Screen before);
    void updateSession();
    void settingsIntent(SettingsIntent intent);
    bool savePreferences(const Settings& settings, const std::string& nickname, std::string& error);

    bool sdlInitialized_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Cursor* crosshair_ = nullptr;
    World world_;
    BotGameController bots_;
    Effects effects_;
    bool wasConnected_ = false;
    Menu menu_;
    GameplayInputGate inputGate_;
    InputEventState eventInput_;
    SettingsController settings_;
    std::unique_ptr<WindowSettings> windowSettings_;
    FramePacer pacer_;
    ConnectionController connections_;
    Profile profile_;
    std::string directoryOverride_;
    std::uint32_t lastRound_ = 0;
};

} // namespace cubeshot::client
