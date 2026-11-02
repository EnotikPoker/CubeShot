#include "client/Input.h"
#include "client/Menu.h"
#include "client/Profile.h"
#include "client/SettingsController.h"
#include "client/SettingsRenderer.h"
#include "client/SettingsStore.h"
#include "client/UiText.h"
#include "client/VisualAssets.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace cubeshot;
using namespace cubeshot::client;

namespace
{
void expect(bool condition, const char* text)
{
    if (!condition)
        throw std::runtime_error(text);
}

SDL_Event key(SDL_Scancode code, bool down = true, SDL_Keymod mod = SDL_KMOD_NONE)
{
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = code;
    e.key.mod = mod;
    return e;
}

SettingsIntent send(SettingsController& controller, SDL_Scancode code, SDL_Keymod mod = SDL_KMOD_NONE)
{
    return controller.event(key(code, true, mod), nullptr, nullptr);
}

struct FakeWindow : WindowSettings
{
    DisplaySettings current;
    bool fail = false, ready = true, failRestore = false;
    unsigned calls = 0;

    bool apply(const DisplaySettings& settings, std::string& error) override
    {
        ++calls;
        if (fail)
        {
            fail = false;
            error = "unsupported";
            return false;
        }
        if (failRestore && !settings.fullscreen)
        {
            error = "restore failed";
            return false;
        }
        current = settings;
        return true;
    }

    bool settled(const DisplaySettings&) const override
    {
        return ready;
    }

    std::vector<WindowSize> availableSizes() const override
    {
        return {windowSizes.begin(), windowSizes.end()};
    }
};

void modelAndInput()
{
    Settings defaults;
    expect(validBindings(defaults.bindings) && validDisplay(defaults.display), "Defaults valid");
    expect(defaults.display.fps == 120 && !defaults.display.vsync && !defaults.display.fullscreen,
           "Initial display preserves low-latency windowed play");
    auto bindings = defaults.bindings;
    const auto old = bindings;
    expect(!assignBinding(bindings, InputAction::Jump, 1, {BindingDevice::Keyboard, SDL_SCANCODE_A}).empty(),
           "Reject occupied key");
    expect(bindings == old, "Conflict leaves all bindings intact");
    expect(!assignBinding(bindings, InputAction::Jump, 0, {}).empty(), "Cannot delete last required binding");
    expect(!assignBinding(bindings, InputAction::Jump, 0, {BindingDevice::Keyboard, SDL_SCANCODE_ESCAPE})
                .empty(),
           "Esc stays reserved");
    expect(assignBinding(bindings, InputAction::Jump, 1, {BindingDevice::Mouse, SDL_BUTTON_RIGHT}).empty(),
           "Keyboard and mouse supported for every action");
    expect(assignBinding(bindings, InputAction::Shoot, 0, {BindingDevice::Keyboard, SDL_SCANCODE_F}).empty(),
           "Fire can use keyboard");
    expect(assignBinding(bindings, InputAction::Restart, 0, {}).empty(), "Restart can be unbound");
    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    keys[SDL_SCANCODE_A] = keys[SDL_SCANCODE_SPACE] = keys[SDL_SCANCODE_F] = true;
    expect(actionHeld(InputAction::Left, bindings, keys.data(), keys.size(), 0) &&
               actionHeld(InputAction::Jump, bindings, keys.data(), keys.size(), 0) &&
               actionHeld(InputAction::Shoot, bindings, keys.data(), keys.size(), 0),
           "Movement jump and fire are independent");
    InputEventState events;
    auto pressed = events.press(key(SDL_SCANCODE_F), bindings);
    expect(pressed[3], "Keyboard fire press delivered immediately");
    expect(!events.press(key(SDL_SCANCODE_F), bindings)[3], "Held fire does not auto-repeat");
    events.press(key(SDL_SCANCODE_F, false), bindings);
    expect(events.press(key(SDL_SCANCODE_F), bindings)[3], "Second click after release fires");
    events.press(key(SDL_SCANCODE_SPACE), bindings);
    SDL_Event mouse{};
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    mouse.button.button = SDL_BUTTON_RIGHT;
    expect(!events.press(mouse, bindings)[2], "Second alias held simultaneously does not repeat jump");
    events.press(key(SDL_SCANCODE_SPACE, false), bindings);
    mouse.type = SDL_EVENT_MOUSE_BUTTON_UP;
    events.press(mouse, bindings);
    mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    expect(events.press(mouse, bindings)[2], "Mouse jump re-arms after both aliases released");
    GameplayInputGate gate;
    gate.reset({0, true, true, {1, 0}});
    expect(!gate.filter({0, true, true, {1, 0}}).shoot, "Resume requires release");
    gate.filter({0, false, false, {1, 0}});
    expect(gate.filter({0, true, true, {1, 0}}).shoot, "New click after release allowed");
}

void storage()
{
    const auto folder = std::filesystem::temp_directory_path() /
                        std::filesystem::path("cubeshot-settings-" + std::to_string(SDL_GetTicksNS()));
    std::filesystem::create_directories(folder);

    struct Cleanup
    {
        std::filesystem::path path;

        ~Cleanup()
        {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{folder};

    const auto file = folder / "settings.json";
    expect(loadSettings(file).display == Settings{}.display, "Missing file uses defaults");
    Settings desired;
    desired.display.fps = 30;
    desired.display.vsync = true;
    desired.bindings[3][0] = {BindingDevice::Keyboard, SDL_SCANCODE_F};
    std::string error;
    expect(saveSettings(desired, file, error), "Persist valid settings");
    expect(loadSettings(file).display == desired.display && loadSettings(file).bindings == desired.bindings,
           "Round trip");
    desired.display.fps = 240;
    expect(saveSettings(desired, file, error) && loadSettings(file).display.fps == 240,
           "Atomic replace existing file works on every OS");
    nlohmann::json data;
    {
        std::ifstream input(file);
        input >> data;
    }
    data["bindings"][0][0] = data["bindings"][1][0];
    {
        std::ofstream output(file);
        output << data;
    }
    auto loaded = loadSettings(file);
    expect(loaded.bindings == Settings{}.bindings && loaded.display == desired.display,
           "Corrupt bindings reset independently of display");
    data["display"]["fps"] = 17;
    {
        std::ofstream output(file);
        output << data;
    }
    expect(loadSettings(file).display == Settings{}.display, "Unknown FPS falls back to safe defaults");
    data["display"]["fps"] = 120.5;
    {
        std::ofstream output(file);
        output << data;
    }
    expect(loadSettings(file).display == Settings{}.display,
           "Fractional settings are rejected instead of truncated");
    {
        std::ofstream output(file);
        output << "broken";
    }
    expect(loadSettings(file).bindings == Settings{}.bindings, "Damaged JSON does not throw");
    expect(!saveSettings(desired, folder / "missing" / "settings.json", error) && !error.empty(),
           "Write failures are reported");
    const auto profile = folder / "profile.json";
    {
        std::ofstream output(profile);
        output << R"({"nickname":"Ёжик","directory_url":"https://example.org"})";
    }
    auto legacy = loadProfile(profile);
    expect(legacy.nickname == "Ёжик" && legacy.directoryUrl == "https://example.org",
           "Existing profile is compatible without migration");
    legacy.nickname = "Новый";
    expect(saveProfile(legacy, profile, error) && loadProfile(profile).nickname == "Новый",
           "Nickname is saved in the original profile");
    expect(loadProfile(profile).directoryUrl == legacy.directoryUrl, "Nickname edit preserves service URL");
}

void transactionAndEditing()
{
    SettingsController controller;
    FakeWindow window;
    controller.initialize(Settings{}, "Старый");
    int saves = 0;
    Settings saved;
    std::string name;
    const auto save = [&](const Settings& s, const std::string& n, std::string&)
    {
        ++saves;
        saved = s;
        name = n;
        return true;
    };
    send(controller, SDL_SCANCODE_RETURN);
    expect(controller.editingNickname(), "Player field starts editing");
    send(controller, SDL_SCANCODE_A, SDL_KMOD_CTRL);
    controller.appendNickname("Ёжик");
    controller.backspaceNickname();
    expect(controller.draftNickname() == "Ёжи", "Backspace removes whole UTF-8 letter");
    controller.appendNickname("🙂");
    expect(controller.draftNickname() == "Ёжи", "Reject unsupported characters");
    send(controller, SDL_SCANCODE_TAB);
    expect(controller.tab() == SettingsTab::Player, "Typing does not navigate tabs");
    send(controller, SDL_SCANCODE_ESCAPE);
    expect(controller.draftNickname() == "Старый" && !controller.editingNickname(),
           "Esc cancels current text edit");
    controller.clearNickname();
    controller.appendNickname("Новый");
    controller.selectTab(SettingsTab::Display);
    send(controller, SDL_SCANCODE_RIGHT); // fullscreen
    controller.apply(window, save, 1000);
    expect(controller.previewing() && saves == 0 && controller.nickname() == "Старый",
           "Display preview does not save nickname or settings");
    window.ready = false;
    controller.confirm(window, save);
    expect(saves == 0, "No confirmation before asynchronous display switch settles");
    controller.tick(window, 15999);
    expect(controller.previewing(), "Preview active before deadline");
    controller.tick(window, 16000);
    expect(!controller.previewing() && !window.current.fullscreen && saves == 0,
           "15 second timeout restores previous display");
    window.ready = true;
    controller.apply(window, save, 20000);
    controller.confirm(window, save);
    expect(saves == 1 && saved.display.fullscreen && name == "Новый",
           "Confirmation commits whole transaction");
    controller.begin();
    controller.clearNickname();
    controller.appendNickname("Отмена");
    controller.cancel(window);
    expect(controller.draftNickname() == "Новый", "Cancel restores last committed nickname");
    controller.resetDraft();
    expect(controller.draftNickname() == "Новый" && controller.draft().display == Settings{}.display,
           "Reset preserves identity");
    window.fail = true;
    controller.apply(window, save, 40000);
    expect(!controller.previewing() && saves == 1 && window.current.fullscreen,
           "Unsupported display rolls back without saving");
    controller.begin();
    controller.clearNickname();
    controller.apply(window, save, 50000);
    expect(name == "Игрок", "Empty nickname becomes default on Apply");
    controller.begin();
    controller.clearNickname();
    controller.appendNickname("В памяти");
    controller.apply(
        window,
        [](const Settings&, const std::string&, std::string& e)
        {
            e = "disk full";
            return false;
        },
        60000);
    expect(controller.nickname() == "В памяти" && !controller.message().empty(),
           "Write error keeps effective state and reports failure");
    controller.apply(window, save, 61000);
    expect(name == "В памяти", "Retry saves the applied settings");
    controller.selectTab(SettingsTab::Controls);
    expect(send(controller, SDL_SCANCODE_RETURN) == SettingsIntent::None && controller.capturing(),
           "Slot captures next press rather than activating Enter");
    send(controller, SDL_SCANCODE_ESCAPE);
    expect(!controller.capturing(), "Esc cancels binding capture");
}

void menuAndTiming()
{
    Menu menu;
    menu.moveSelection(1);
    expect(menu.activate() == MenuAction::OpenSettings, "Main opens settings action");
    menu.openSettings();
    menu.closeSettings();
    expect(menu.screen() == Screen::Main, "Return to main");
    Match match;
    match.connect(1);
    match.connect(2);
    match.setReady(1, true);
    match.setReady(2, true);
    menu.networkState(match.snapshot(), 1);
    menu.escape();
    menu.openSettings();
    match.update(3);
    menu.networkState(match.snapshot(), 1);
    expect(menu.screen() == Screen::Settings, "Snapshots do not dismiss settings");
    menu.closeSettings();
    expect(menu.screen() == Screen::Paused, "Round returns to personal pause");
    menu.openSettings();
    match.disconnect(2);
    menu.networkState(match.snapshot(), 1);
    expect(menu.screen() == Screen::Settings, "Peer left updates data without discarding edits");
    menu.closeSettings();
    expect(menu.screen() == Screen::Lobby, "Return respects current server phase");
    menu.open(Screen::Settings);
    menu.open(Screen::OnlineBrowser);
    expect(menu.screen() == Screen::OnlineBrowser, "Connection error can force browser outside overlay");
    FramePacer pacer;
    expect(pacer.due(0, 30), "First frame due");
    pacer.presented(0, 30);
    unsigned networkPolls = 0;
    for (std::uint64_t ns = 1000000; ns < 33000000; ns += 1000000)
    {
        ++networkPolls;
        expect(!pacer.due(ns, 30), "Frame cap delays render only");
        expect(pacer.waitMilliseconds(ns, 30) <= 8, "No long sleep at low frame limit");
    }
    expect(networkPolls == 32 && pacer.due(34000000, 30),
           "Input and network can run between render deadlines");
    expect(pacer.due(1, 0) && pacer.waitMilliseconds(1, 0) == 0, "Unlimited disables software wait");
}

void renderPages()
{
    using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;
    using Renderer = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
    Surface surface(SDL_CreateSurface(1100, 700, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    Renderer renderer(SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
    expect(bool(renderer), "Software renderer available");
    cubeshot::client::VisualAssets art(renderer.get());
    expect(SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255) && SDL_RenderClear(renderer.get()) &&
               drawText(renderer.get(), U"=", {0, 0}, 1, {255, 255, 255, 255}) &&
               SDL_RenderPresent(renderer.get()),
           "Punctuation keys have readable glyphs");
    Uint8 red = 0, green = 0, blue = 0, alpha = 0;
    unsigned lit = 0;
    for (int y = 0; y < 14; ++y)
        for (int x = 0; x < 14; ++x)
        {
            SDL_ReadSurfacePixel(surface.get(), x, y, &red, &green, &blue, &alpha);
            lit += red > 100;
        }
    expect(lit > 0, "Equals key has a baked readable glyph");
    expect(SDL_SetRenderLogicalPresentation(renderer.get(), 960, 540, SDL_LOGICAL_PRESENTATION_LETTERBOX),
           "Settings renderer uses fixed logical coordinates");
    float logicalX = 0, logicalY = 0;
    expect(SDL_RenderCoordinatesFromWindow(renderer.get(), 550, 350, &logicalX, &logicalY) &&
               std::abs(logicalX - 480) < .01f && std::abs(logicalY - 270) < .01f,
           "Scaled letterboxed mouse coordinates match logical scene");
    SettingsController controller;
    controller.initialize(Settings{}, "Тестовый Игрок");
    for (auto tab : {SettingsTab::Player, SettingsTab::Display, SettingsTab::Controls})
    {
        controller.selectTab(tab);
        expect(renderSettings(renderer.get(), controller, 0, false) && SDL_RenderPresent(renderer.get()),
               "Every settings tab renders");
    }
    controller.selectTab(SettingsTab::Display);
    send(controller, SDL_SCANCODE_RIGHT);
    FakeWindow window;
    controller.apply(window, [](const Settings&, const std::string&, std::string&) { return true; }, 0);
    expect(renderSettings(renderer.get(), controller, 1000, true) && SDL_RenderPresent(renderer.get()),
           "Confirmation countdown renders");
}
} // namespace

int main()
{
    if (!SDL_Init(0))
        return 1;
    int result = 0;
    try
    {
        modelAndInput();
        storage();
        transactionAndEditing();
        menuAndTiming();
        renderPages();
        std::cout << "Settings, input, persistence, rollback and navigation passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
