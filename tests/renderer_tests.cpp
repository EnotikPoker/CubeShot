#include "client/SceneRenderer.h"
#include "client/WorldView.h"
#include "game/World.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{

constexpr int imageSize = 128;
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Surface drawWorld(const cubeshot::World& world)
{
    Surface surface(SDL_CreateSurface(960, 540, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    expect(surface != nullptr, "Create test surface");
    Renderer renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    expect(renderer != nullptr, "Create software renderer");
    expect(cubeshot::client::renderScene(renderer.get(), world), "Render test scene");
    return surface;
}

Surface drawPlayer(cubeshot::Vec2 aim)
{
    cubeshot::Arena arena;
    arena.bounds = {{0, 0}, {imageSize, imageSize}};
    // Положение руки точно в (64, 64) для сравнения зеркальных пикселей.
    arena.spawnPoints = {{40, 64 - cubeshot::Player::defaultSize * 0.55f}};
    cubeshot::World world(arena);
    cubeshot::PlayerInput input;
    input.aimDirection = aim;
    world.setPlayerInput(cubeshot::World::localPlayerId, input);
    return drawWorld(world);
}

SDL_Color pixel(SDL_Surface* surface, int x, int y)
{
    SDL_Color color{};
    expect(SDL_ReadSurfacePixel(surface, static_cast<int>(210 + x * 540.f / 128),
                                static_cast<int>(y * 540.f / 128), &color.r, &color.g, &color.b, &color.a),
           "Read rendered pixel");
    return color;
}

bool sameColor(SDL_Color first, SDL_Color second)
{
    return first.r == second.r && first.g == second.g && first.b == second.b && first.a == second.a;
}

void testMirroredWeapon(cubeshot::Vec2 aim)
{
    const auto right = drawPlayer(aim);
    const auto left = drawPlayer({-aim.x, aim.y});
    for (int y = 1; y < imageSize - 1; ++y)
    {
        for (int x = 1; x < imageSize - 1; ++x)
        {
            const SDL_Color first = pixel(right.get(), x, y);
            const int mirroredX = imageSize - 1 - x;
            bool interior = true;
            // Сравниваем внутренние пиксели: округление краёв треугольников
            // SDL не должно влиять на проверку положения рукояти и ствола.
            for (int dy = -1; interior && dy <= 1; ++dy)
            {
                for (int dx = -1; interior && dx <= 1; ++dx)
                {
                    interior = sameColor(first, pixel(right.get(), x + dx, y + dy));
                }
            }
            expect(!interior || sameColor(first, pixel(left.get(), mirroredX, y)),
                   "Weapon aiming left mirrors right-facing weapon without turning upside down");
        }
    }
}

void testHealthBars()
{
    using namespace cubeshot;
    Arena arena;
    arena.bounds = {{0, 0}, {imageSize, imageSize}};
    arena.platforms = {{{0, 100}, {imageSize, 28}}};
    arena.spawnPoints = {{4, 52}, {76, 52}};
    World world(arena);
    const SDL_Color green{85, 210, 125, 255};
    const SDL_Color empty{80, 35, 45, 255};
    const SDL_Color background{24, 28, 38, 255};
    for (int hits = 0; hits <= 5; ++hits)
    {
        const auto surface = drawWorld(world);
        int filledPixels = 0;
        for (int x = 76; x < 124; ++x)
        {
            const SDL_Color color = pixel(surface.get(), x, 42);
            if (hits == 5)
            {
                expect(sameColor(color, background), "Dead player's health bar disappears");
            }
            else
            {
                expect(sameColor(color, green) || sameColor(color, empty),
                       "Health bar shows remaining health and missing health");
                filledPixels += sameColor(color, green);
            }
        }
        expect(std::abs(filledPixels - 48.0f * (100 - 20 * hits) / 100.0f) <= 1.0f,
               "Health bar width follows real damage proportionally");
        expect(sameColor(pixel(surface.get(), 20, 42), green), "Undamaged player's health bar stays full");
        if (hits < 5)
        {
            world.setPlayerInput(1, {});
            PlayerInput input;
            input.shoot = true;
            world.setPlayerInput(1, input);
            world.update(0.3f);
        }
    }
    world = World(arena);
    const auto reset = drawWorld(world);
    expect(sameColor(pixel(reset.get(), 120, 42), green), "Reset restores full health bar");
    world.setPlayerInput(2, PlayerInput{0.0f, true});
    world.update(1.0f / 120.0f);
    const auto jumped = drawWorld(world);
    expect(sameColor(pixel(jumped.get(), 100, 37), green) &&
               sameColor(pixel(jumped.get(), 100, 42), background),
           "Health bar follows character during jump");
    world.update(0.12f);
    const auto ceiling = drawWorld(world);
    expect(sameColor(pixel(ceiling.get(), 100, 3), green),
           "Health bar stays visible when character reaches arena ceiling");
}

} // namespace

int main()
{
    if (!SDL_Init(0))
    {
        std::cerr << SDL_GetError() << '\n';
        return 1;
    }
    int result = 0;
    try
    {
        testMirroredWeapon({1, 0});
        testMirroredWeapon({3, -4});
        testMirroredWeapon({3, 4});
        testHealthBars();
        std::cout << "Renderer tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
