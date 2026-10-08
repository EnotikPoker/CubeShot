#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int, char**)
{
    constexpr int windowWidth = 960;
    constexpr int windowHeight = 540;
    constexpr float floorHeight = 100.0f;
    constexpr float playerSize = 48.0f;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("CubeShot", windowWidth, windowHeight,
                                    0, &window, &renderer)) {
        SDL_Log("Window creation failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    const SDL_FRect floor{0.0f, windowHeight - floorHeight,
                          static_cast<float>(windowWidth), floorHeight};
    const SDL_FRect player{(windowWidth - playerSize) / 2.0f,
                           floor.y - playerSize, playerSize, playerSize};

    bool running = true;
    int exitCode = 0;
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

        if (!SDL_SetRenderDrawColor(renderer, 24, 28, 38, 255) ||
            !SDL_RenderClear(renderer) ||
            !SDL_SetRenderDrawColor(renderer, 95, 110, 125, 255) ||
            !SDL_RenderFillRect(renderer, &floor) ||
            !SDL_SetRenderDrawColor(renderer, 80, 175, 255, 255) ||
            !SDL_RenderFillRect(renderer, &player) ||
            !SDL_RenderPresent(renderer)) {
            SDL_Log("Rendering failed: %s", SDL_GetError());
            exitCode = 1;
            break;
        }

        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
