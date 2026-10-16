#pragma once

#include "game/World.h"
#include "client/Input.h"
#include "client/Menu.h"
#include "client/HostProcess.h"
#include "network/Session.h"
#include "network/Discovery.h"
#include "network/Directory.h"
#include "client/Profile.h"
#include <memory>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Cursor;

namespace cubeshot::client {

class Application {
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
    void leaveRoom();
    void screenChanged(Screen before);
    void updateNetwork();
    void updateDirectory();

    bool sdlInitialized_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Cursor* crosshair_ = nullptr;
    World world_;
    Menu menu_;
    GameplayInputGate inputGate_;
    std::unique_ptr<network::Client> network_;
    std::unique_ptr<network::Discovery> discovery_;
    HostProcess host_;
    bool hosting_ = false, onlineSession_ = false;
    Profile profile_;
    network::Directory directory_;
    std::string directoryOverride_, reservationToken_;
    std::uint64_t nextDirectoryRefresh_ = 0;
    std::uint32_t lastRound_ = 0;
};

} // namespace cubeshot::client
