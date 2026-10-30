#include "client/Effects.h"
#include "client/SceneRenderer.h"
#include "client/SettingsController.h"
#include "client/SettingsStore.h"
#include "client/VisualAssets.h"
#include "client/WorldView.h"
#include "game/World.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace cubeshot;
using namespace cubeshot::client;

namespace
{
void expect(bool b, const char* message)
{
    if (!b)
        throw std::runtime_error(message);
}

bool near(float a, float b)
{
    return std::abs(a - b) < .01f;
}

void effects()
{
    const auto arena = Arena::createDuel();
    World world(arena);
    Effects fx;
    fx.update(0, world.players(), arena, {}, 1);
    GameEvent predicted{0, 0, 7, 1, EventKind::Shot, 1, 0, {140, 410}, {1, 0}};
    fx.update(.01f, world.players(), arena, {predicted}, 1);
    expect(fx.pose(1).shot > 0 && std::hypot(fx.shakeOffset().x, fx.shakeOffset().y) > 0,
           "Own shot immediately animates recoil and shake");
    auto confirmed = predicted;
    confirmed.id = 1;
    fx.update(.06f, world.players(), arena, {confirmed}, 1);
    expect(fx.pose(1).shot < .05f, "Authority acknowledgement does not replay predicted recoil");
    GameEvent hit{2, 20, 7, 1, EventKind::Hit, 1, 2, {850, 420}, {1, 0}};
    fx.update(.01f, world.players(), arena, {hit}, 1);
    const auto count = fx.particles().size();
    expect(count > 0 && fx.pose(2).hit > 0 && !fx.splashes().empty(),
           "Confirmed hit creates blood and hit animation");
    fx.update(0, world.players(), arena, {hit}, 1);
    expect(fx.particles().size() == count, "Duplicate authoritative event produces no extra blood");
    for (int i = 0; i < 180; ++i)
        fx.update(1.f / 120, world.players(), arena, {}, 1);
    expect(!fx.stains().empty(), "Blood particles leave surface stains");
    for (int i = 0; i < 600; ++i)
    {
        hit.id = 10 + i;
        fx.update(0, world.players(), arena, {hit}, 1);
    }
    expect(fx.particles().size() <= Effects::maxParticles && fx.stains().size() <= Effects::maxStains &&
               fx.splashes().size() <= 32,
           "Presentation resources remain bounded");
    fx.update(0, world.players(), arena, {}, 1, false, false);
    expect(
        std::none_of(fx.particles().begin(), fx.particles().end(), [](const auto& p) { return p.blood; }) &&
            fx.stains().empty() && near(fx.shakeOffset().x, 0),
        "Disabling effects clears current blood and shake");
    auto dead = world.players();
    dead[1].health = 0;
    fx.update(.1f, dead, arena, {}, 1);
    expect(fx.pose(2).death > 0 && !fx.pose(2).alive, "Dead body has an independent visual animation");
    fx.clear();
    expect(fx.pose(2).death == 0 && fx.particles().empty(), "World reset clears effects");
}

void transformAndRendering()
{
    SDL_Surface* surface = SDL_CreateSurface(960, 540, SDL_PIXELFORMAT_RGBA32);
    expect(surface, "Create surface");
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
    expect(renderer, "Create renderer");
    auto art = std::make_unique<VisualAssets>(renderer);
    for (const auto& info : mapCatalog())
    {
        World world(Arena::create(info.id, true));
        WorldView view(world.arena().bounds);
        for (const auto& p : world.players())
        {
            const auto screen = view.toScreen(p.weaponOrigin());
            const auto back = view.toWorld(screen);
            expect(near(back.x, p.weaponOrigin().x) && near(back.y, p.weaponOrigin().y),
                   "Aim coordinate transform roundtrip on all maps");
        }
        Effects fx;
        fx.update(.01f, world.players(), world.arena(), world.takeEvents(), 1);
        expect(renderScene(renderer, world, false, &fx), "Every theme renders with effects");
        world.setPlayerInput(1, {1, true, true, {1, -1}});
        world.update(.05f);
        fx.update(.05f, world.players(), world.arena(), world.takeEvents(), 1);
        expect(renderScene(renderer, world, false, &fx), "Animated scene renders after simultaneous actions");
    }
    art.reset();
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(surface);
}

void settings()
{
    auto dir = std::filesystem::temp_directory_path() /
               std::filesystem::u8path("cubeshot-effects-" + std::to_string(SDL_GetPerformanceCounter()));
    std::filesystem::create_directories(dir);
    auto file = dir / "settings.json";
    Settings s;
    s.blood = false;
    s.shake = false;
    std::string error;
    expect(saveSettings(s, file, error), "Save effect switches");
    const auto loaded = loadSettings(file);
    expect(!loaded.blood && !loaded.shake, "Effect switches persist");
    SettingsController controller;
    controller.initialize(s, "Игрок");
    controller.selectTab(SettingsTab::Effects);
    auto items = controller.items();
    int values = 0;
    for (const auto& item : items)
        if (item.kind == SettingsItem::Kind::EffectValue)
            ++values;
    expect(values == 2, "Effects tab exposes both switches");
    controller.resetDraft();
    expect(controller.draft().blood && controller.draft().shake, "Reset restores effects defaults");
    std::filesystem::remove_all(dir);
}
} // namespace

int main()
{
    if (!SDL_Init(0))
        return 1;
    int result = 0;
    try
    {
        effects();
        transformAndRendering();
        settings();
        std::cout << "Effects and view checks passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
