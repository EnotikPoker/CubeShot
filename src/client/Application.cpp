#include "client/Application.h"
#include "client/AbilityUI.h"
#include "client/BuildInspector.h"
#include "client/MenuRenderer.h"
#include "client/SceneRenderer.h"
#include "client/SettingsRenderer.h"
#include "client/SettingsStore.h"
#include "client/UiText.h"
#include "client/WorldView.h"
#include "network/Identity.h"
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
    audio_.shutdown();
    visuals_.reset();
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
    try
    {
        visuals_ = std::make_unique<VisualAssets>(renderer_);
    }
    catch (const std::exception& e)
    {
        SDL_Log("Visual resources failed: %s", e.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "CubeShot",
                                 "Не удалось загрузить графику. Распакуйте архив игры полностью.", window_);
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
    audio_.setSettings(preferences.audio);
    audio_.initialize(VisualAssets::directory() / "audio");
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
    audio_.mixer.play(menu_.playing() ? SoundCue::Click : SoundCue::Back, .45f, 0, 3);
    world_.clearPlayerInput(World::localPlayerId);
    bots_.synchronize(menu_);
    bots_.clearInput();
    inputGate_.reset(rawInput());
    eventInput_.synchronize();
    SDL_SetCursor(menu_.playing() ? crosshair_ : SDL_GetDefaultCursor());
    if (menu_.bots() && (menu_.screen() == Screen::Paused || menu_.screen() == Screen::Settings))
        bots_.clearInput();
    if (menu_.screen() == Screen::Address)
        SDL_StartTextInput(window_);
    else if (before == Screen::Address)
        SDL_StopTextInput(window_);
}

bool Application::handle(MenuAction action)
{
    if (action != MenuAction::None)
        audio_.mixer.play(SoundCue::Click, .4f, 0, 3);
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
        sounds_.clear();
        audio_.clear();
        lastRound_ = 0;
    }
    if (action == MenuAction::EditTest)
    {
        const auto& edit = menu_.testPanel().edit();
        bool accepted = world_.editTest(edit, menu_.testDraft());
        menu_.completeTestEdit(accepted);
        if (accepted && (edit.operation == TestOperation::RestoreMap ||
                         edit.operation == TestOperation::Clear || edit.operation == TestOperation::Respawn))
        {
            effects_.clear();
            sounds_.clear();
            audio_.clear();
        }
    }
    if (action == MenuAction::StartTest || action == MenuAction::ApplyTest)
    {
        world_ = World{Arena::create(menu_.selectedMap(), true), 4};
        world_.equip(1, menu_.testWeapon());
        for (unsigned i = 0; i < 4; ++i)
        {
            world_.setAbilities(i + 1, menu_.testConfiguration().abilities[i]);
            world_.equip(i + 1, menu_.testConfiguration().weapons[i]);
            world_.setActives(i + 1, menu_.testConfiguration().actives[i]);
            world_.setHealth(i + 1, menu_.testConfiguration().healthPercent[i]);
        }
        effects_.clear();
        sounds_.clear();
        audio_.clear();
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
        {
            effects_.clear();
            sounds_.clear();
            audio_.clear();
        }
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
            sounds_.clear();
            audio_.clear();
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
    // Exercise the application loop, not just the mixer: offline polling must
    // preserve menu and Test sounds until all their samples have been mixed.
    audio_.mixer.setSettings({100, 100, 100});
    auto audibleAcrossUpdates = [&]
    {
        for (int frame = 0; frame < 4; ++frame)
        {
            updateSession();
            std::array<float, 128> samples{};
            audio_.mixer.mix(samples.data(), samples.size() / 2);
            if (std::none_of(samples.begin(), samples.end(), [](float value) { return value != 0; }))
                return false;
        }
        return true;
    };
    if (!audio_.mixer.play(SoundCue::Click) || !audibleAcrossUpdates())
    {
        SDL_Log("Portable audio test: menu sound was interrupted by application polling");
        return 1;
    }
    audio_.clear();
    world_.requestAction(World::localPlayerId, false, true, {0, -1});
    world_.update(1.f / 120);
    sounds_.update(1.f / 120, world_.players(), world_.arena(), world_.takeEvents(), World::localPlayerId,
                   world_.entities(), world_.bullets());
    if (!audibleAcrossUpdates())
    {
        SDL_Log("Portable audio test: Test shot was interrupted by application polling");
        return 1;
    }
    audio_.clear();
    sounds_.clear();
    audio_.mixer.setSettings(settings_.active().audio);
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
    Arena effectArena = Arena::create(MapId::Duel);
    auto arenaForEffects = [&](const WorldSnapshot& state) -> const Arena&
    {
        if (effectArena.map != state.map)
            effectArena = Arena::create(state.map);
        effectArena.applyTerrain(state.terrain);
        return effectArena;
    };
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
            if (event.type == SDL_EVENT_MOUSE_WHEEL && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB] &&
                (connections_.session() || bots_.match()))
            {
                float mx = 0, my = 0;
                SDL_GetMouseState(&mx, &my);
                Vec2 logical;
                SDL_RenderCoordinatesFromWindow(renderer_, mx, my, &logical.x, &logical.y);
                auto slot = logical.x < 480 ? 0 : 1;
                buildScroll_[slot] =
                    std::clamp(buildScroll_[slot] - int(event.wheel.y), 0, int(abilityCount) - 10);
            }
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
                if ((session || menu_.bots()) && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB] &&
                    (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_RIGHT))
                {
                    auto state = session ? *session->snapshot() : bots_.match()->snapshot();
                    inspectedPlayer_ =
                        (inspectedPlayer_ + state.players.size() + (key == SDL_SCANCODE_RIGHT ? 1 : -1)) %
                        state.players.size();
                    buildScroll_ = {};
                    continue;
                }
                if (key == SDL_SCANCODE_TAB && !session && !menu_.bots() &&
                    (before == Screen::Playing || menu_.liveTestPanel()))
                {
                    if (menu_.liveTestPanel())
                        menu_.escape();
                    else
                        menu_.openTestPanel(true, Screen::Playing);
                }
                else if (key == SDL_SCANCODE_ESCAPE)
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
                    if ((menu_.screen() == Screen::Armory && key == SDL_SCANCODE_LEFT) ||
                        key == SDL_SCANCODE_UP || (key == SDL_SCANCODE_W && before != Screen::Address))
                        menu_.moveSelection(-1);
                    if ((menu_.screen() == Screen::Armory && key == SDL_SCANCODE_RIGHT) ||
                        key == SDL_SCANCODE_DOWN || (key == SDL_SCANCODE_S && before != Screen::Address))
                        menu_.moveSelection(1);
                    if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)
                        action = menu_.activate();
                }
            }
            // Only rising aggregate action edges produce discrete gameplay events.
            if (before == Screen::Playing && menu_.playing() &&
                presses[static_cast<std::size_t>(InputAction::Restart)] && !session && !menu_.bots())
            {
                {
                    world_ = World{Arena::create(menu_.selectedMap(), true), 4};
                    world_.equip(1, menu_.testWeapon());
                    for (unsigned i = 0; i < 4; ++i)
                    {
                        world_.setAbilities(i + 1, menu_.testConfiguration().abilities[i]);
                        world_.equip(i + 1, menu_.testConfiguration().weapons[i]);
                        world_.setActives(i + 1, menu_.testConfiguration().actives[i]);
                        world_.setHealth(i + 1, menu_.testConfiguration().healthPercent[i]);
                    }
                    effects_.clear();
                    sounds_.clear();
                    audio_.clear();
                }
                inputGate_.reset(rawInput());
                accumulator = 0;
            }
            const bool jumpEvent = presses[static_cast<std::size_t>(InputAction::Jump)];
            const bool shotEvent = presses[static_cast<std::size_t>(InputAction::Shoot)];
            const bool reloadEvent = presses[static_cast<std::size_t>(InputAction::Reload)];
            unsigned activeMask = (presses[static_cast<std::size_t>(InputAction::ActiveQ)] ? 1u : 0u) |
                                  (presses[static_cast<std::size_t>(InputAction::ActiveE)] ? 2u : 0u);
            if (before == Screen::Playing && menu_.playing() &&
                (jumpEvent || shotEvent || reloadEvent || activeMask))
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
                activeMask &= (input.activeQ ? 1u : 0u) | (input.activeE ? 2u : 0u);
                if (session)
                {
                    if (activeMask)
                        session->action(false, false, input.aimDirection, false, activeMask);
                    if (reloadEvent)
                        session->action(false, false, input.aimDirection, true);
                    if (jumpEvent && input.jump)
                        session->action(true, false, input.aimDirection);
                    if (shotEvent && input.shoot)
                        session->action(false, true, input.aimDirection);
                }
                else if (bots_.match())
                {
                    if (activeMask)
                        bots_.action(false, false, input.aimDirection, false, activeMask);
                    if (reloadEvent)
                        bots_.action(false, false, input.aimDirection, true);
                    if (jumpEvent && input.jump)
                        bots_.action(true, false, input.aimDirection);
                    if (shotEvent && input.shoot)
                        bots_.action(false, true, input.aimDirection);
                }
                else
                {
                    if (activeMask)
                        world_.requestAction(World::localPlayerId, false, false, input.aimDirection, 0, false,
                                             activeMask);
                    if (reloadEvent)
                        world_.requestAction(World::localPlayerId, false, false, input.aimDirection, 0, true);
                    world_.setPlayerInput(World::localPlayerId, input);
                }
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
        if ((menu_.playing() || (menu_.bots() && menu_.screen() == Screen::Armory)) && !session)
        {
            accumulator += std::min(elapsed, .1);
            const auto input = menu_.playing() ? inputGate_.filter(rawInput()) : PlayerInput{};
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
                sounds_.clear();
                audio_.clear();
                inputGate_.reset(rawInput());
                eventInput_.synchronize();
            }
            auto events = bots_.match()->takeEvents();
            const float soundDt = menu_.screen() == Screen::Paused || menu_.screen() == Screen::Settings
                                      ? 0.f
                                      : static_cast<float>(elapsed);
            sounds_.update(soundDt, s.players, arenaForEffects(s), events, 1, s.entities, s.bullets);
            sounds_.match(s.phase, s.round, s.remaining, s.weaponChoice, 1);
            effects_.update(
                menu_.playing() || menu_.screen() == Screen::BotResult || menu_.screen() == Screen::Armory
                    ? static_cast<float>(elapsed)
                    : 0.f,
                s.players, arenaForEffects(s), events, 1, settings_.active().blood, settings_.active().shake);
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
            auto soundPlayers = visual.players;
            if (auto predicted = session->predictedPlayer())
                for (auto& p : soundPlayers)
                    if (p.id == session->playerId())
                        p = *predicted;
            sounds_.update(static_cast<float>(elapsed), soundPlayers, arenaForEffects(s), events,
                           session->playerId(), s.entities, s.bullets);
            sounds_.match(s.phase, s.round, s.remaining, s.weaponChoice, session->playerId());
            effects_.update(static_cast<float>(elapsed), visual.players, arenaForEffects(s), events,
                            session->playerId(), settings_.active().blood, settings_.active().shake);
        }
        else if (!bots_.match())
        {
            auto events = world_.takeEvents();
            sounds_.update(menu_.playing() ? static_cast<float>(elapsed) : 0.f, world_.players(),
                           world_.arena(), events, World::localPlayerId, world_.entities(), world_.bullets());
            effects_.update(menu_.playing() ? static_cast<float>(elapsed) : 0.f, world_.players(),
                            world_.arena(), events, World::localPlayerId, settings_.active().blood,
                            settings_.active().shake);
        }
        const auto volumes =
            menu_.screen() == Screen::Settings ? settings_.draft().audio : settings_.active().audio;
        static AudioSettings lastVolumes;
        const bool volumeChanged = !(volumes == lastVolumes);
        audio_.setSettings(volumes);
        if (volumeChanged && menu_.screen() == Screen::Settings)
        {
            audio_.mixer.play(volumes.combat != lastVolumes.combat ? SoundCue::HitConfirm : SoundCue::Click,
                              .5f, 0, 3);
        }
        lastVolumes = volumes;
        audio_.pump((SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) != 0);
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
        if (menu_.liveTestPanel())
        {
            const auto& p = world_.players()[menu_.testTarget()];
            std::u32string activeText;
            for (unsigned i = 0; i < 2; ++i)
            {
                auto a = p.actives[i];
                if (i)
                    activeText += U" · ";
                activeText += (i ? U"E: " : U"Q: ") + std::u32string(activeDefinition(a.kind).name) + U" " +
                              (a.cooldown > 0 ? asciiText(std::to_string(int(std::ceil(a.cooldown)))) + U" с"
                                              : U"готово");
            }
            menu_.testActiveStatus(activeText);
            menu_.testStatus(U"HP " + asciiText(std::to_string(p.health)) + U"/" +
                             asciiText(std::to_string(p.healthLimit())) + U" · Щиты " +
                             asciiText(std::to_string(p.shields)) + U" · Патроны " +
                             asciiText(std::to_string(p.weapon.ammo)) + U" · Перегрев " +
                             asciiText(std::to_string(static_cast<int>(p.weapon.heat * 100))) +
                             U"%\nДжетпак " +
                             asciiText(std::to_string(static_cast<int>(p.jetpackFuel * 50))) + U"% · " +
                             (p.weapon.reloading ? U"Идёт перезарядка" : U"Оружие готово") +
                             (p.burning > 0 ? U" · Горит" : U""));
        }
        const bool sceneVisible =
            menu_.playing() || menu_.liveTestPanel() || menu_.screen() == Screen::Paused ||
            (session && menu_.screen() == Screen::Result) ||
            (bots_.match() && menu_.screen() == Screen::BotResult) || menu_.screen() == Screen::Armory;
        bool rendered = true;
        if (sceneVisible && menu_.playing())
        {
            float mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            Vec2 cursor;
            SDL_RenderCoordinatesFromWindow(renderer_, mx, my, &cursor.x, &cursor.y);
            rendered = rendered && drawCombatFeedback(renderer_, effects_, cursor);
        }
        if (sceneVisible)
        {
            if (session && session->snapshot())
                rendered =
                    renderSnapshot(renderer_, session->interpolated(), session->playerId(), &effects_) &&
                    renderMatchHud(renderer_, *session->snapshot(), session->playerId());
            else if (bots_.match())
            {
                const auto s = bots_.match()->snapshot();
                rendered = renderSnapshot(renderer_, s, 1, &effects_) && renderMatchHud(renderer_, s, 1);
            }
            else
                rendered = renderScene(renderer_, world_, false, &effects_) &&
                           drawWeaponHud(renderer_, world_.players()[0]);
        }
        if (sceneVisible)
        {
            std::array<std::u32string, 2> keys{
                bindingName(settings_.active().bindings[unsigned(InputAction::ActiveQ)][0]),
                bindingName(settings_.active().bindings[unsigned(InputAction::ActiveE)][0])};
            rendered = rendered && drawActiveHud(renderer_, ownPlayer(), keys);
        }
        if (menu_.screen() == Screen::Settings)
            rendered = renderSettings(renderer_, settings_, SDL_GetTicks(), menu_.lan());
        else if (!menu_.playing())
            rendered = rendered && renderMenu(renderer_, menu_);
        if (sceneVisible && (session || bots_.match()) && SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_TAB])
        {
            auto state = session ? *session->snapshot() : bots_.match()->snapshot();
            float mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            Vec2 logical;
            SDL_RenderCoordinatesFromWindow(renderer_, mx, my, &logical.x, &logical.y);
            unsigned index = inspectedPlayer_ % state.players.size();
            std::vector<Player> selected{state.players[index]};
            std::array<std::u32string, 2> names{
                {network::nicknameText(state.names[index]) + U" · Стрелки: участник", U""}};
            rendered = rendered && drawBuildInspector(renderer_, selected, names, buildScroll_, logical);
        }
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
