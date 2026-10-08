#pragma once

#include "game/World.h"

struct SDL_Window;
struct SDL_Renderer;

namespace cubeshot::client {

class Application {
public:
    Application() = default;
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    bool initialize();

    bool sdlInitialized_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    World world_;
};

} // namespace cubeshot::client
