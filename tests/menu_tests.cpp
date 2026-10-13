#include "client/BitmapFont.h"
#include "client/Input.h"
#include "client/Menu.h"
#include "client/MenuRenderer.h"

#include <SDL3/SDL.h>

#include <iostream>
#include <memory>
#include <stdexcept>

namespace {

using namespace cubeshot;
using namespace cubeshot::client;

void expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void startTest(Menu& menu)
{
    expect(menu.activate() == MenuAction::None && menu.screen() == Screen::Modes,
           "Play opens mode selection");
    menu.moveSelection(3);
    expect(menu.activate() == MenuAction::StartTest && menu.playing(),
           "Test requests a fresh scene");
}

void testNavigation()
{
    Menu menu;
    expect(menu.screen() == Screen::Main && !menu.playing() && menu.buttons().size() == 3,
           "Client starts at main menu");
    menu.escape();
    expect(menu.screen() == Screen::Main, "Escape at main menu does not quit");
    menu.moveSelection(1);
    expect(menu.activate() == MenuAction::None && menu.screen() == Screen::Main &&
               !menu.message().empty(), "Main settings show placeholder without launching game");
    menu.moveSelection(-1);
    menu.activate();
    expect(menu.screen() == Screen::Modes && menu.buttons().size() == 5,
           "Mode screen includes three future modes, Test and Back");
    for (int i = 0; i < 3; ++i) {
        expect(menu.activate() == MenuAction::None && menu.screen() == Screen::Modes &&
                   !menu.message().empty(), "Future modes do not start gameplay");
        menu.moveSelection(1);
    }
    menu.moveSelection(1);
    menu.activate();
    expect(menu.screen() == Screen::Main && menu.message().empty(), "Back returns to main menu");
    menu.activate();
    menu.escape();
    expect(menu.screen() == Screen::Main, "Escape backs out of mode selection");
    startTest(menu);
    menu.escape();
    expect(menu.screen() == Screen::Paused && !menu.playing() && menu.buttons().size() == 4,
           "Escape opens pause and stops gameplay");
    menu.moveSelection(1);
    menu.activate();
    expect(menu.screen() == Screen::Paused && !menu.message().empty(),
           "Pause settings leave the game paused");
    menu.moveSelection(-1);
    expect(menu.activate() == MenuAction::None && menu.playing(),
           "Continue resumes existing scene without requesting reset");
    menu.escape();
    menu.escape();
    expect(menu.playing(), "Escape also resumes paused scene");
    menu.escape();
    menu.moveSelection(2);
    menu.activate();
    expect(menu.screen() == Screen::Main, "Pause menu can return to main menu");
    startTest(menu);
    menu.escape();
    menu.moveSelection(3);
    expect(menu.activate() == MenuAction::Quit, "Pause menu can quit application");
    Menu main;
    main.moveSelection(-1);
    expect(main.selection() == 2 && main.activate() == MenuAction::Quit,
           "Selection wraps and main menu can quit");
}

Vec2 center(const MenuButton& button)
{
    return {button.bounds.position.x + button.bounds.size.x / 2,
            button.bounds.position.y + button.bounds.size.y / 2};
}

void testMouseClicks()
{
    Menu menu;
    const Vec2 play = center(menu.buttons()[0]);
    const Vec2 quit = center(menu.buttons()[2]);
    expect(menu.release(play) == MenuAction::None && menu.screen() == Screen::Main,
           "Mouse release without button press cannot activate menu");
    menu.press(play);
    menu.release(quit);
    expect(menu.screen() == Screen::Main, "Dragging between buttons cancels click");
    menu.press(play);
    menu.release({0, 0});
    expect(menu.screen() == Screen::Main, "Release outside button cancels click");
    menu.press(play);
    menu.cancelClick();
    menu.release(play);
    expect(menu.screen() == Screen::Main, "Losing focus cancels armed click");
    menu.press(play);
    menu.release(play);
    expect(menu.screen() == Screen::Modes, "Click opens mode selection");
    expect(menu.release(play) == MenuAction::None && menu.screen() == Screen::Modes,
           "Same mouse release does not activate the next screen");
    const Vec2 test = center(menu.buttons()[3]);
    menu.hover(test);
    expect(menu.selection() == 3, "Pointer highlights hovered button");
    menu.press(test);
    expect(menu.release(test) == MenuAction::StartTest && menu.playing(),
           "Test scene starts on complete click");
}

void testHeldInput()
{
    GameplayInputGate gate;
    PlayerInput held;
    held.moveDirection = 1;
    held.jump = held.shoot = true;
    held.aimDirection = {-1, 0};
    gate.reset(held);
    auto filtered = gate.filter(held);
    expect(!filtered.jump && !filtered.shoot && filtered.moveDirection == 1 &&
               filtered.aimDirection.x == -1, "Resume blocks held actions without blocking movement or aim");
    held.shoot = false;
    gate.filter(held);
    held.shoot = true;
    filtered = gate.filter(held);
    expect(filtered.shoot && !filtered.jump, "Fresh shot works while old jump remains held");
    held.jump = false;
    gate.filter(held);
    held.jump = true;
    expect(gate.filter(held).jump, "Jump works after release and new press");
    gate.reset({});
    expect(gate.filter(held).shoot && gate.filter(held).jump,
           "Entering scene with released buttons allows fresh actions");
}

void testMenuRendering()
{
    const auto destroySurface = &SDL_DestroySurface;
    const auto destroyRenderer = &SDL_DestroyRenderer;
    std::unique_ptr<SDL_Surface, decltype(destroySurface)> surface(
        SDL_CreateSurface(960, 540, SDL_PIXELFORMAT_RGBA32), destroySurface);
    expect(surface != nullptr, "Create menu test surface");
    std::unique_ptr<SDL_Renderer, decltype(destroyRenderer)> renderer(
        SDL_CreateSoftwareRenderer(surface.get()), destroyRenderer);
    expect(renderer != nullptr, "Create menu test renderer");
    Menu menu;
    for (int screen = 0; screen < 3; ++screen) {
        expect(renderMenu(renderer.get(), menu) && SDL_RenderPresent(renderer.get()),
               "Russian menus render without window or font dependencies");
        Uint8 r, g, b, a;
        const Rect button = menu.buttons()[0].bounds;
        expect(SDL_ReadSurfacePixel(surface.get(), static_cast<int>(button.position.x),
                                    static_cast<int>(button.position.y), &r, &g, &b, &a) &&
                   r == 80 && g == 175 && b == 255,
               "Selected button has visible blue border");
        if (screen == 0) menu.activate();
        else if (screen == 1) {
            menu.moveSelection(3);
            menu.activate();
            menu.escape();
        }
    }
    expect(textWidth(U"Играть", 3) == 105, "Cyrillic text measures letters, not UTF-8 bytes");
}

} // namespace

int main()
{
    if (!SDL_Init(0)) return 1;
    int result = 0;
    try {
        testNavigation();
        testMouseClicks();
        testHeldInput();
        testMenuRendering();
        std::cout << "Menu tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
