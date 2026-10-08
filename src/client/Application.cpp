#include "client/Application.h"

#include "client/Input.h"
#include "client/SceneRenderer.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace cubeshot::client {

Application::~Application()
{
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    if (sdlInitialized_) {
        SDL_Quit();
    }
}

bool Application::initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return false;
    }
    sdlInitialized_ = true;

    const Vec2 size = world_.arena().bounds.size;
    if (!SDL_CreateWindowAndRenderer("CubeShot", static_cast<int>(size.x),
                                    static_cast<int>(size.y), 0, &window_, &renderer_)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        return false;
    }
    if (!SDL_SetRenderLogicalPresentation(renderer_, static_cast<int>(size.x),
                                         static_cast<int>(size.y),
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        SDL_Log("Renderer setup failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

int Application::run()
{
    if (!initialize()) {
        return 1;
    }

    constexpr double stepSeconds = 1.0 / 120.0;
    double accumulator = 0.0;
    Uint64 previousTime = SDL_GetTicksNS();
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        if (!running) {
            break;
        }

        const Uint64 now = SDL_GetTicksNS();
        const double elapsed = static_cast<double>(now - previousTime) / 1'000'000'000.0;
        previousTime = now;
        // После долгой паузы не пытаемся разом наверстать всё пропущенное время.
        accumulator += std::min(elapsed, 0.1);
        world_.setPlayerInput(World::localPlayerId, readPlayerInput(window_));
        while (accumulator >= stepSeconds) {
            world_.update(static_cast<float>(stepSeconds));
            accumulator -= stepSeconds;
        }

        if (!renderScene(renderer_, world_)) {
            SDL_Log("Rendering failed: %s", SDL_GetError());
            return 1;
        }
        SDL_Delay(16);
    }
    return 0;
}

} // namespace cubeshot::client
