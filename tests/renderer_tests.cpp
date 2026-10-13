#include "client/SceneRenderer.h"
#include "game/World.h"

#include <SDL3/SDL.h>

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

constexpr int imageSize = 128;
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;

void expect(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Surface drawPlayer(cubeshot::Vec2 aim)
{
    Surface surface(SDL_CreateSurface(imageSize, imageSize, SDL_PIXELFORMAT_RGBA32),
                    SDL_DestroySurface);
    expect(surface != nullptr, "Create test surface");
    Renderer renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    expect(renderer != nullptr, "Create software renderer");

    cubeshot::Arena arena;
    arena.bounds = {{0, 0}, {imageSize, imageSize}};
    // Положение руки точно в (64, 64) для сравнения зеркальных пикселей.
    arena.spawnPoints = {{40, 64 - cubeshot::Player::defaultSize * 0.55f}};
    cubeshot::World world(arena);
    cubeshot::PlayerInput input;
    input.aimDirection = aim;
    world.setPlayerInput(cubeshot::World::localPlayerId, input);
    expect(cubeshot::client::renderScene(renderer.get(), world), "Render test scene");
    return surface;
}

SDL_Color pixel(SDL_Surface* surface, int x, int y)
{
    SDL_Color color{};
    expect(SDL_ReadSurfacePixel(surface, x, y, &color.r, &color.g, &color.b, &color.a),
           "Read rendered pixel");
    return color;
}

bool sameColor(SDL_Color first, SDL_Color second)
{
    return first.r == second.r && first.g == second.g && first.b == second.b &&
           first.a == second.a;
}

void testMirroredWeapon(cubeshot::Vec2 aim)
{
    const auto right = drawPlayer(aim);
    const auto left = drawPlayer({-aim.x, aim.y});
    for (int y = 1; y < imageSize - 1; ++y) {
        for (int x = 1; x < imageSize - 1; ++x) {
            const SDL_Color first = pixel(right.get(), x, y);
            const int mirroredX = imageSize - 1 - x;
            bool interior = true;
            // Сравниваем внутренние пиксели: округление краёв треугольников
            // SDL не должно влиять на проверку положения рукояти и ствола.
            for (int dy = -1; interior && dy <= 1; ++dy) {
                for (int dx = -1; interior && dx <= 1; ++dx) {
                    interior = sameColor(first, pixel(right.get(), x + dx, y + dy));
                }
            }
            expect(!interior || sameColor(first, pixel(left.get(), mirroredX, y)),
                   "Weapon aiming left mirrors right-facing weapon without turning upside down");
        }
    }
}

} // namespace

int main()
{
    if (!SDL_Init(0)) {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    int result = 0;
    try {
        testMirroredWeapon({1, 0});
        testMirroredWeapon({3, -4});
        testMirroredWeapon({3, 4});
        std::cout << "Renderer tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
