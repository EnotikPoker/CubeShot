#include "client/SceneRenderer.h"

#include "game/World.h"

#include <SDL3/SDL.h>

namespace cubeshot::client {
namespace {

bool fillRect(SDL_Renderer* renderer, const Rect& rectangle)
{
    const SDL_FRect rect{rectangle.position.x, rectangle.position.y,
                         rectangle.size.x, rectangle.size.y};
    return SDL_RenderFillRect(renderer, &rect);
}

} // namespace

bool renderScene(SDL_Renderer* renderer, const World& world)
{
    if (!SDL_SetRenderDrawColor(renderer, 24, 28, 38, 255) ||
        !SDL_RenderClear(renderer) ||
        !SDL_SetRenderDrawColor(renderer, 95, 110, 125, 255)) {
        return false;
    }
    for (const Rect& platform : world.arena().platforms) {
        if (!fillRect(renderer, platform)) {
            return false;
        }
    }

    if (!SDL_SetRenderDrawColor(renderer, 80, 175, 255, 255)) {
        return false;
    }
    for (const Player& player : world.players()) {
        if (!fillRect(renderer, player.bounds())) {
            return false;
        }
    }
    return SDL_RenderPresent(renderer);
}

} // namespace cubeshot::client
