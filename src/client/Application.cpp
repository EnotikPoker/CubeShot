#include "client/Application.h"

#include "client/Input.h"
#include "client/MenuRenderer.h"
#include "client/SceneRenderer.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace cubeshot::client {

Application::~Application()
{
    if (crosshair_) {
        SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_DestroyCursor(crosshair_);
    }
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
    crosshair_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    if (!crosshair_ || !SDL_SetCursor(SDL_GetDefaultCursor())) {
        SDL_Log("Crosshair setup failed: %s", SDL_GetError());
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
                break;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                menu_.cancelClick();
                world_.clearPlayerInput(World::localPlayerId);
            }
            if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) == 0) continue;

            const Screen before = menu_.screen();
            MenuAction action = MenuAction::None;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                const auto key = event.key.scancode;
                if (key == SDL_SCANCODE_ESCAPE) {
                    menu_.escape();
                } else if (menu_.playing() && key == SDL_SCANCODE_R) {
                    world_ = World{};
                    inputGate_.reset(readPlayerInput(window_, renderer_, world_.players().front()));
                    accumulator = 0.0;
                    previousTime = SDL_GetTicksNS();
                } else if (!menu_.playing()) {
                    if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_W) menu_.moveSelection(-1);
                    if (key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_S) menu_.moveSelection(1);
                    if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) {
                        action = menu_.activate();
                    }
                }
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && !menu_.playing()) {
                Vec2 logical;
                if (SDL_RenderCoordinatesFromWindow(renderer_, event.motion.x, event.motion.y,
                                                     &logical.x, &logical.y)) menu_.hover(logical);
            }
            if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                if (menu_.playing()) {
                    // Сохраняем короткий клик между шагами, но не клики по меню.
                    auto input = readPlayerInput(window_, renderer_, world_.players().front());
                    input.shoot = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    world_.setPlayerInput(World::localPlayerId, inputGate_.filter(input));
                } else {
                    Vec2 logical;
                    if (SDL_RenderCoordinatesFromWindow(renderer_, event.button.x, event.button.y,
                                                         &logical.x, &logical.y)) {
                        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) menu_.press(logical);
                        else action = menu_.release(logical);
                    }
                }
            }
            if (action == MenuAction::Quit) {
                running = false;
                break;
            }
            if (action == MenuAction::StartTest) world_ = World{};
            if (before != menu_.screen()) {
                world_.clearPlayerInput(World::localPlayerId);
                inputGate_.reset(readPlayerInput(window_, renderer_, world_.players().front()));
                accumulator = 0.0;
                previousTime = SDL_GetTicksNS();
                if (!SDL_SetCursor(menu_.playing() ? crosshair_ : SDL_GetDefaultCursor())) {
                    SDL_Log("Cursor change failed: %s", SDL_GetError());
                    return 1;
                }
            }
        }
        if (!running) {
            break;
        }

        const Uint64 now = SDL_GetTicksNS();
        const double elapsed = static_cast<double>(now - previousTime) / 1'000'000'000.0;
        previousTime = now;
        if (menu_.playing()) {
            // После паузы время меню не добавляется к симуляции.
            accumulator += std::min(elapsed, 0.1);
            world_.setPlayerInput(World::localPlayerId, inputGate_.filter(
                readPlayerInput(window_, renderer_, world_.players().front())));
            while (accumulator >= stepSeconds) {
                world_.update(static_cast<float>(stepSeconds));
                accumulator -= stepSeconds;
            }
        } else {
            accumulator = 0.0;
        }

        const bool sceneVisible = menu_.playing() || menu_.screen() == Screen::Paused;
        if ((sceneVisible && !renderScene(renderer_, world_, false)) ||
            (!menu_.playing() && !renderMenu(renderer_, menu_)) || !SDL_RenderPresent(renderer_)) {
            SDL_Log("Rendering failed: %s", SDL_GetError());
            return 1;
        }
        SDL_Delay(16);
    }
    return 0;
}

} // namespace cubeshot::client
