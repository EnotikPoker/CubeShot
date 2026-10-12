#include "client/SceneRenderer.h"

#include "game/World.h"

#include <SDL3/SDL.h>

#include <cmath>

namespace cubeshot::client {
namespace {

bool fillRect(SDL_Renderer* renderer, const Rect& rectangle)
{
    const SDL_FRect rect{rectangle.position.x, rectangle.position.y,
                         rectangle.size.x, rectangle.size.y};
    return SDL_RenderFillRect(renderer, &rect);
}

bool gunPart(SDL_Renderer* renderer, Vec2 origin, Vec2 direction,
             float x, float y, float width, float height, SDL_FColor color,
             bool mirrored = false)
{
    const auto vertex = [&](float along, float across) {
        // При прицеливании влево рукоять остаётся снизу от ствола.
        across *= mirrored ? -1.0f : 1.0f;
        return SDL_Vertex{{origin.x + direction.x * along - direction.y * across,
                           origin.y + direction.y * along + direction.x * across}, color, {0, 0}};
    };
    const SDL_Vertex vertices[]{vertex(x, y), vertex(x + width, y),
                                vertex(x + width, y + height), vertex(x, y + height)};
    const int indices[]{0, 1, 2, 0, 2, 3};
    return SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
}

bool renderWeapon(SDL_Renderer* renderer, const Player& player)
{
    const Vec2 origin = player.weaponOrigin();
    const Vec2 aim = player.aimDirection;
    const bool mirrored = aim.x < 0.0f;
    // Рука, рукоять, ствол и светлая верхняя грань пистолета.
    return gunPart(renderer, origin, aim, 0, -3, 15, 8, {0.94f, 0.73f, 0.49f, 1}, mirrored) &&
           gunPart(renderer, origin, aim, 12, 0, 8, 15, {0.22f, 0.25f, 0.3f, 1}, mirrored) &&
           gunPart(renderer, origin, aim, 10, -5, Weapon::barrelLength - 10, 10,
                   {0.7f, 0.75f, 0.82f, 1}, mirrored) &&
           gunPart(renderer, origin, aim, 11, -5, Weapon::barrelLength - 12, 3,
                   {0.91f, 0.94f, 1, 1}, mirrored);
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

    for (const Player& player : world.players()) {
        if (!player.alive()) {
            continue;
        }
        const bool local = player.id == World::localPlayerId;
        if (!SDL_SetRenderDrawColor(renderer, local ? 80 : 230, local ? 175 : 85,
                                    local ? 255 : 85, 255) ||
            !fillRect(renderer, player.bounds()) || !renderWeapon(renderer, player)) {
            return false;
        }
    }

    for (const Bullet& bullet : world.bullets()) {
        const float speed = std::hypot(bullet.velocity.x, bullet.velocity.y);
        const Vec2 direction = speed > 0.0f
            ? Vec2{bullet.velocity.x / speed, bullet.velocity.y / speed} : Vec2{1, 0};
        if (!gunPart(renderer, bullet.position, direction, -15, -1.5f, 15, 3,
                      {1, 0.55f, 0.12f, 1}) ||
            !SDL_SetRenderDrawColor(renderer, 255, 240, 150, 255) ||
            !fillRect(renderer, {{bullet.position.x - Bullet::radius,
                                  bullet.position.y - Bullet::radius},
                                 {Bullet::radius * 2, Bullet::radius * 2}})) {
            return false;
        }
    }
    return SDL_RenderPresent(renderer);
}

} // namespace cubeshot::client
