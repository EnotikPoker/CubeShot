#include "client/Application.h"
#include "client/BitmapFont.h"
#include "client/MenuRenderer.h"
#include "client/SceneRenderer.h"
#include "client/SettingsRenderer.h"
#include "client/SettingsStore.h"
#include "client/WorldView.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace cubeshot::client
{
Application::~Application()
{
    connections_.stop();
    if (crosshair_)
    {
        SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_DestroyCursor(crosshair_);
    }
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    if (sdlInitialized_)
        SDL_Quit();
}

bool Application::initialize()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return false;
    }
    sdlInitialized_ = true;
    profile_ = loadProfile();
    if (!directoryOverride_.empty())
        profile_.directoryUrl = directoryOverride_;
    connections_.setDirectoryUrl(profile_.directoryUrl);
    menu_.setNickname(profile_.nickname);
    if (!SDL_CreateWindowAndRenderer("CubeShot " CUBESHOT_VERSION, 960, 540, 0, &window_, &renderer_) ||
        !SDL_SetRenderLogicalPresentation(renderer_, 960, 540, SDL_LOGICAL_PRESENTATION_LETTERBOX))
    {
        SDL_Log("Window/renderer creation failed: %s", SDL_GetError());
        return false;
    }
    windowSettings_ = std::make_unique<SdlWindowSettings>(window_, renderer_);
    auto preferences = loadSettings();
    std::string error;
    if (!windowSettings_->apply(preferences.display, error))
    {
        SDL_Log("Saved display unavailable: %s", error.c_str());
        menu_.setMessage(U"Экран недоступен. Восстановлены стандартные настройки");
        preferences.display = DisplaySettings{};
        if (!windowSettings_->apply(preferences.display, error))
        {
            SDL_Log("Cannot apply default display: %s", error.c_str());
        }
    }
    settings_.initialize(preferences, profile_.nickname);
    eventInput_.synchronize();
    crosshair_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    return crosshair_ && SDL_SetCursor(SDL_GetDefaultCursor());
}

Player Application::ownPlayer() const
{
    const auto* session = connections_.session();
    if (session && session->predictedPlayer())
        return *session->predictedPlayer();
    if (session && session->snapshot())
    {
        for (const auto& p : session->snapshot()->players)
            if (p.id == session->playerId())
                return p;
    }
    if (bots_.match())
        return bots_.match()->match().world().players().front();
    return world_.players().front();
}

Rect Application::arenaBounds() const
{
    if (bots_.match())
        return bots_.match()->match().world().arena().bounds;
    auto* session = connections_.session();
    return session && session->snapshot() ? Arena::create(session->snapshot()->map).bounds
                                          : world_.arena().bounds;
}

PlayerInput Application::rawInput() const
{
    return readPlayerInput(window_, renderer_, ownPlayer(), settings_.active().bindings, arenaBounds());
}

void Application::screenChanged(Screen before)
{
    if (before == menu_.screen())
        return;
    world_.clearPlayerInput(World::localPlayerId);
    bots_.synchronize(menu_);
    bots_.clearInput();
    inputGate_.reset(rawInput());
    eventInput_.synchronize();
    SDL_SetCursor(menu_.playing() ? crosshair_ : SDL_GetDefaultCursor());
    if (menu_.screen() == Screen::Address)
        SDL_StartTextInput(window_);
    else if (before == Screen::Address)
        SDL_StopTextInput(window_);
}

bool Application::handle(MenuAction action)
{
    if (action == MenuAction::Quit)
        return false;
    if (action == MenuAction::OpenSettings)
    {
        settings_.begin();
        settings_.setAvailableSizes(windowSettings_->availableSizes());
        menu_.openSettings();
    }
    connections_.handle(action, menu_, profile_.nickname);
    bots_.handle(action, menu_, profile_.nickname);
    if (action == MenuAction::StartBots || action == MenuAction::BotRematch)
    {
        effects_.clear();
        lastRound_ = 0;
    }
    if (action == MenuAction::StartTest)
    {
        world_ = World{Arena::create(menu_.selectedMap(), true), 4};
        effects_.clear();
    }
    if ((!connections_.session() && !bots_.match()) ||
        (connections_.session() && connections_.session()->connecting()))
        lastRound_ = 0;
    return true;
}

void Application::updateSession()
{
    connections_.update(menu_);
    auto* session = connections_.session();
    if (!session)
    {
        if (bots_.match())
            return;
        if (wasConnected_)
            effects_.clear();
        wasConnected_ = false;
        lastRound_ = 0;
        return;
    }
    if (session->connected() && session->snapshot())
    {
        wasConnected_ = true;
        const auto& snapshot = *session->snapshot();
        if (lastRound_ != snapshot.round)
        {
            lastRound_ = snapshot.round;
            effects_.clear();
            inputGate_.reset(rawInput());
        }
        const auto input = menu_.playing() ? inputGate_.filter(rawInput())
                                           : PlayerInput{0, false, false, ownPlayer().aimDirection};
        session->control(input);
    }
}

bool Application::savePreferences(const Settings& settings, const std::string& nickname, std::string& error)
{
    profile_.nickname = nickname;
    menu_.setNickname(nickname);
    std::string profileError, settingsError;
    const bool savedProfile = saveProfile(profile_, &profileError);
    const bool savedSettings = saveSettings(settings, settingsError);
    error = profileError + " " + settingsError;
    return savedProfile && savedSettings;
}

void Application::settingsIntent(SettingsIntent intent)
{
    settings_.tick(*windowSettings_, SDL_GetTicks());
    const auto save = [this](const Settings& s, const std::string& n, std::string& e)
    { return savePreferences(s, n, e); };
    if (intent == SettingsIntent::Apply)
    {
        settings_.apply(*windowSettings_, save, SDL_GetTicks());
        inputGate_.reset(rawInput());
        eventInput_.synchronize();
    }
    if (intent == SettingsIntent::Confirm)
    {
        settings_.confirm(*windowSettings_, save);
        inputGate_.reset(rawInput());
        eventInput_.synchronize();
    }
    if (intent == SettingsIntent::Revert)
        settings_.cancelPreview(*windowSettings_);
    if (intent == SettingsIntent::Close)
    {
        settings_.cancel(*windowSettings_);
        SDL_StopTextInput(window_);
        menu_.closeSettings();
    }
}

int Application::smokeTest()
{
    if (!initialize() || !renderMenu(renderer_, menu_) || !SDL_RenderPresent(renderer_) ||
        !renderScene(renderer_, world_))
        return 1;
    BotMatch bots(MapId::Duel, BotDifficulty::Medium, "Smoke", 42);
    for (int tick = 0; tick < 480; ++tick)
        bots.update(1.f / 120);
    const auto botState = bots.snapshot();
    if (botState.phase != MatchPhase::Round || botState.players.size() != 2 ||
        botState.players[1].position.x >= 832 || !renderSnapshot(renderer_, botState, 1) ||
        !renderMatchHud(renderer_, botState, 1) || !SDL_RenderPresent(renderer_))
        return 1;
    HostProcess host;
    if (!host.start())
    {
        SDL_Log("Packaged server could not start: %s", host.error().c_str());
        return 1;
    }
    network::Client client;
    if (!client.connect("127.0.0.1:27015", host.token()))
        return 1;
    const auto deadline = SDL_GetTicks() + 15000;
    while (SDL_GetTicks() < deadline && host.running())
    {
        SDL_PumpEvents();
        client.poll();
        if (client.connected() && client.playerId() == 1 && client.snapshot())
        {
            const bool rendered = renderSnapshot(renderer_, *client.snapshot(), client.playerId()) &&
                                  SDL_RenderPresent(renderer_);
            client.disconnect();
            host.stop();
            SDL_Log("Portable smoke test: %s", rendered ? "passed" : "render failed");
            return rendered ? 0 : 1;
        }
        if (!client.connecting() && !client.connected())
            break;
        SDL_Delay(1);
    }
    SDL_Log("Portable LAN connection failed: %s; %s", client.error().c_str(), host.error().c_str());
    return 1;
}

int Application::run()
{
    if (!initialize())
        return 1;
    constexpr double stepSeconds = 1.0 / 120;
    double accumulator = 0;
    Uint64 previousTime = SDL_GetTicksNS();
    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            auto* session = connections_.session();
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
                break;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
            {
                menu_.cancelClick();
                world_.clearPlayerInput(World::localPlayerId);
                bots_.clearInput();
                const auto beforeFocus = menu_.screen();
                menu_.loseFocus();
                screenChanged(beforeFocus);
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
            {
                inputGate_.reset(rawInput());
                eventInput_.synchronize();
            }
            if (event.type == SDL_EVENT_WINDOW_DISPLAY_CHANGED && menu_.screen() == Screen::Settings)
                settings_.setAvailableSizes(windowSettings_->availableSizes());
            if (menu_.screen() == Screen::Settings && event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                settings_.event(event, window_, renderer_);
            if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) == 0)
                continue;
            const Screen before = menu_.screen();
            if (before == Screen::Settings)
            {
                const bool editing = settings_.editingNickname();
                settingsIntent(settings_.event(event, window_, renderer_));
                if (editing != settings_.editingNickname())
                {
                    if (settings_.editingNickname())
                        SDL_StartTextInput(window_);
                    else
                        SDL_StopTextInput(window_);
                }
                screenChanged(before);
                continue;
            }
            const auto presses = eventInput_.press(event, settings_.active().bindings);
            MenuAction action = MenuAction::None;
            if (event.type == SDL_EVENT_TEXT_INPUT && (before == Screen::Address))
                menu_.text(event.text.text);
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
            {
                const auto key = event.key.scancode;
                if (key == SDL_SCANCODE_ESCAPE)
                {
                    if (before == Screen::Connecting)
                        action = MenuAction::CancelConnection;
                    else if (before == Screen::Lobby || before == Screen::Result)
                        action = MenuAction::LeaveRoom;
                    else
                        menu_.escape();
                }
                else if (!menu_.playing())
                {
                    if (key == SDL_SCANCODE_BACKSPACE && (before == Screen::Address))
                        menu_.backspace();
                    if (key == SDL_SCANCODE_V && (event.key.mod & SDL_KMOD_CTRL) &&
                        (before == Screen::Address))
                    {
                        char* text = SDL_GetClipboardText();
                        if (text)
                        {
                            menu_.text(text);
                            SDL_free(text);
                        }
                    }
                    if (key == SDL_SCANCODE_UP || (key == SDL_SCANCODE_W && before != Screen::Address))
                        menu_.moveSelection(-1);
                    if (key == SDL_SCANCODE_DOWN || (key == SDL_SCANCODE_S && before != Screen::Address))
                        menu_.moveSelection(1);
                    if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)
                        action = menu_.activate();
                }
            }
            // Only rising aggregate action edges produce discrete gameplay events.
            if (menu_.playing() && presses[static_cast<std::size_t>(InputAction::Restart)] && !session &&
                !menu_.bots())
            {
                {
                    world_ = World{Arena::create(menu_.selectedMap(), true), 4};
                    effects_.clear();
                }
                inputGate_.reset(rawInput());
                accumulator = 0;
            }
            const bool jumpEvent = presses[static_cast<std::size_t>(InputAction::Jump)];
            const bool shotEvent = presses[static_cast<std::size_t>(InputAction::Shoot)];
            if (menu_.playing() && (jumpEvent || shotEvent))
            {
                auto input = rawInput();
                if (jumpEvent)
                    input.jump = true;
                if (shotEvent)
                {
                    input.shoot = true;
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                    {
                        Vec2 logical;
                        if (SDL_RenderCoordinatesFromWindow(renderer_, event.button.x, event.button.y,
                                                            &logical.x, &logical.y))
                        {
                            logical = WorldView(arenaBounds()).toWorld(logical);
                            const auto origin = ownPlayer().weaponOrigin();
                            const Vec2 aim{logical.x - origin.x, logical.y - origin.y};
                            if (std::hypot(aim.x, aim.y) > .0001f)
                                input.aimDirection = aim;
                        }
                    }
                }
                input = inputGate_.filter(input);
                if (session)
                {
                    if (jumpEvent && input.jump)
                        session->action(true, false, input.aimDirection);
                    if (shotEvent && input.shoot)
                        session->action(false, true, input.aimDirection);
                }
                else if (bots_.match())
                {
                    if (jumpEvent && input.jump)
                        bots_.action(true, false, input.aimDirection);
                    if (shotEvent && input.shoot)
                        bots_.action(false, true, input.aimDirection);
                }
                else
                    world_.setPlayerInput(World::localPlayerId, input);
            }
            // Menu clicks always use the left mouse button, independent of the fire binding.
            const bool menuClick =
                (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                event.button.button == SDL_BUTTON_LEFT;
            if (menuClick && !menu_.playing())
            {
                Vec2 logical;
                if (SDL_RenderCoordinatesFromWindow(renderer_, event.button.x, event.button.y, &logical.x,
                                                    &logical.y))
                {
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                        menu_.press(logical);
                    else
                        action = menu_.release(logical);
                }
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && !menu_.playing())
            {
                Vec2 logical;
                if (SDL_RenderCoordinatesFromWindow(renderer_, event.motion.x, event.motion.y, &logical.x,
                                                    &logical.y))
                    menu_.hover(logical);
            }
            running = handle(action);
            screenChanged(before);
            if (before != menu_.screen())
            {
                accumulator = 0;
                previousTime = SDL_GetTicksNS();
            }
            if (!running)
                break;
        }
        if (!running)
            break;
        const Screen before = menu_.screen();
        settings_.tick(*windowSettings_, SDL_GetTicks());
        updateSession();
        if (before == Screen::Settings && menu_.screen() != Screen::Settings)
        {
            settings_.cancel(*windowSettings_);
            SDL_StopTextInput(window_);
        }
        screenChanged(before);
        auto* session = connections_.session();
        const Uint64 now = SDL_GetTicksNS();
        const double elapsed = static_cast<double>(now - previousTime) / 1e9;
        previousTime = now;
        if (menu_.playing() && !session)
        {
            accumulator += std::min(elapsed, .1);
            const auto input = inputGate_.filter(rawInput());
            if (bots_.match())
                bots_.control(input);
            else
                world_.setPlayerInput(World::localPlayerId, input);
            while (accumulator >= stepSeconds)
            {
                if (bots_.match())
                    bots_.update(static_cast<float>(stepSeconds), menu_);
                else
                    world_.update(static_cast<float>(stepSeconds));
                accumulator -= stepSeconds;
            }
        }
        else
            accumulator = 0;
        if (bots_.match())
        {
            const auto s = bots_.match()->snapshot();
            if (s.round != lastRound_)
            {
                lastRound_ = s.round;
                effects_.clear();
                inputGate_.reset(rawInput());
                eventInput_.synchronize();
            }
            effects_.update(
                menu_.playing() || menu_.screen() == Screen::BotResult ? static_cast<float>(elapsed) : 0.f,
                s.players, Arena::create(s.map), bots_.match()->takeEvents(), 1, settings_.active().blood,
                settings_.active().shake);
            const auto previousScreen = menu_.screen();
            bots_.synchronize(menu_);
            screenChanged(previousScreen);
        }
        if (session && session->snapshot())
        {
            auto events = session->takeEvents();
            const auto& s = *session->snapshot();
            events.erase(std::remove_if(events.begin(), events.end(),
                                        [&](const auto& e) { return e.round != s.round; }),
                         events.end());
            const auto visual = session->interpolated();
            effects_.update(static_cast<float>(elapsed), visual.players, Arena::create(s.map), events,
                            session->playerId(), settings_.active().blood, settings_.active().shake);
        }
        else if (!bots_.match())
        {
            effects_.update(menu_.playing() ? static_cast<float>(elapsed) : 0.f, world_.players(),
                            world_.arena(), world_.takeEvents(), World::localPlayerId,
                            settings_.active().blood, settings_.active().shake);
        }
        const int fps =
            settings_.previewing() ? settings_.draft().display.fps : settings_.active().display.fps;
        if (!pacer_.due(now, fps))
        {
            const unsigned wait = pacer_.waitMilliseconds(now, fps);
            if (wait)
                SDL_Delay(wait);
            else
                SDL_DelayNS(200000);
            continue;
        }
        const bool sceneVisible = menu_.playing() || menu_.screen() == Screen::Paused ||
                                  (session && menu_.screen() == Screen::Result) ||
                                  (bots_.match() && menu_.screen() == Screen::BotResult);
        bool rendered = true;
        if (sceneVisible)
        {
            if (session && session->snapshot())
                rendered =
                    renderSnapshot(renderer_, session->interpolated(), session->playerId(), &effects_) &&
                    renderMatchHud(renderer_, *session->snapshot(), session->playerId());
            else if (bots_.match())
            {
                const auto s = bots_.match()->snapshot();
                rendered = renderSnapshot(renderer_, s, 1, &effects_) && renderMatchHud(renderer_, s, 1) &&
                           drawText(renderer_, botDifficultyName(menu_.botDifficulty()), {12, 500}, 1,
                                    {137, 153, 175, 255});
            }
            else
                rendered = renderScene(renderer_, world_, false, &effects_);
        }
        if (menu_.screen() == Screen::Settings)
            rendered = renderSettings(renderer_, settings_, SDL_GetTicks(), menu_.lan());
        else if (!menu_.playing())
            rendered = rendered && renderMenu(renderer_, menu_);
        if (!rendered || !SDL_RenderPresent(renderer_))
        {
            SDL_Log("Rendering failed: %s", SDL_GetError());
            return 1;
        }
        pacer_.presented(now, fps);
    }
    if (settings_.previewing())
        settings_.cancelPreview(*windowSettings_);
    return 0;
}
} // namespace cubeshot::client
