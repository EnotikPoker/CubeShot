#pragma once

#include "client/ConnectionController.h"
#include "client/Input.h"
#include "client/Menu.h"
#include "client/Profile.h"
#include "game/World.h"
#include <cstdint>
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

private:
    bool initialize();
    Player ownPlayer() const;
    PlayerInput rawInput() const;
    bool handle(MenuAction action);
    void screenChanged(Screen before);
    void updateSession();

    bool sdlInitialized_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Cursor* crosshair_ = nullptr;
    World world_;
    Menu menu_;
    GameplayInputGate inputGate_;
    ConnectionController connections_;
    Profile profile_;
    std::string directoryOverride_;
    std::uint32_t lastRound_ = 0;
};

} // namespace cubeshot::client
