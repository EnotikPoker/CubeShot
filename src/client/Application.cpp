#include "client/Application.h"
#include "client/MenuRenderer.h"
#include "client/SceneRenderer.h"
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
    return world_.players().front();
}

PlayerInput Application::rawInput() const
{
    return readPlayerInput(window_, renderer_, ownPlayer());
}

void Application::screenChanged(Screen before)
{
    if (before == menu_.screen())
        return;
    if (before == Screen::Nickname)
        menu_.setNickname(profile_.nickname);
    world_.clearPlayerInput(World::localPlayerId);
    inputGate_.reset(rawInput());
    SDL_SetCursor(menu_.playing() ? crosshair_ : SDL_GetDefaultCursor());
    if (menu_.screen() == Screen::Address || menu_.screen() == Screen::Nickname)
        SDL_StartTextInput(window_);
    else if (before == Screen::Address || before == Screen::Nickname)
        SDL_StopTextInput(window_);
}

bool Application::handle(MenuAction action)
{
    if (action == MenuAction::Quit)
        return false;
    if (action == MenuAction::SaveNickname)
    {
        profile_.nickname = menu_.nickname();
        saveProfile(profile_);
    }
    connections_.handle(action, menu_, profile_.nickname);
    if (action == MenuAction::StartTest)
        world_ = World{};
    if (!connections_.session() || connections_.session()->connecting())
        lastRound_ = 0;
    return true;
}

void Application::updateSession()
{
    connections_.update(menu_, profile_.nickname);
    auto* session = connections_.session();
    if (!session)
    {
        lastRound_ = 0;
        return;
    }
    if (session->connected() && session->snapshot())
    {
        const auto& snapshot = *session->snapshot();
        if (lastRound_ != snapshot.round)
        {
            lastRound_ = snapshot.round;
            inputGate_.reset(rawInput());
        }
        const auto input = menu_.playing() ? inputGate_.filter(rawInput())
                                           : PlayerInput{0, false, false, ownPlayer().aimDirection};
        session->control(input);
    }
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
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
                inputGate_.reset(rawInput());
            if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) == 0)
                continue;
            const Screen before = menu_.screen();
            MenuAction action = MenuAction::None;
            if (event.type == SDL_EVENT_TEXT_INPUT &&
                (before == Screen::Address || before == Screen::Nickname))
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
                else if (menu_.playing() && key == SDL_SCANCODE_R && !session)
                {
                    world_ = World{};
                    inputGate_.reset(rawInput());
                    accumulator = 0;
                }
                else if (!menu_.playing())
                {
                    if (key == SDL_SCANCODE_BACKSPACE &&
                        (before == Screen::Address || before == Screen::Nickname))
                        menu_.backspace();
                    if (key == SDL_SCANCODE_V && (event.key.mod & SDL_KMOD_CTRL) &&
                        (before == Screen::Address || before == Screen::Nickname))
                    {
                        char* text = SDL_GetClipboardText();
                        if (text)
                        {
                            menu_.text(text);
                            SDL_free(text);
                        }
                    }
                    if (key == SDL_SCANCODE_A && (event.key.mod & SDL_KMOD_CTRL) &&
                        before == Screen::Nickname)
                        menu_.clearNickname();
                    if (key == SDL_SCANCODE_UP ||
                        (key == SDL_SCANCODE_W && before != Screen::Address && before != Screen::Nickname))
                        menu_.moveSelection(-1);
                    if (key == SDL_SCANCODE_DOWN ||
                        (key == SDL_SCANCODE_S && before != Screen::Address && before != Screen::Nickname))
                        menu_.moveSelection(1);
                    if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER)
                        action = menu_.activate();
                }
            }
            // Дискретные события сохраняют даже очень короткие клики/прыжки.
            const bool jumpEvent = (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                                   event.key.scancode == SDL_SCANCODE_SPACE && !event.key.repeat;
            const bool shotEvent =
                (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                event.button.button == SDL_BUTTON_LEFT;
            if (menu_.playing() && (jumpEvent || shotEvent))
            {
                auto input = rawInput();
                if (jumpEvent)
                    input.jump = event.type == SDL_EVENT_KEY_DOWN;
                if (shotEvent)
                {
                    input.shoot = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    Vec2 logical;
                    if (SDL_RenderCoordinatesFromWindow(renderer_, event.button.x, event.button.y, &logical.x,
                                                        &logical.y))
                    {
                        const auto origin = ownPlayer().weaponOrigin();
                        const Vec2 aim{logical.x - origin.x, logical.y - origin.y};
                        if (std::hypot(aim.x, aim.y) > 0.0001f)
                            input.aimDirection = aim;
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
                else
                    world_.setPlayerInput(World::localPlayerId, input);
            }
            else if (shotEvent && !menu_.playing())
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
        updateSession();
        screenChanged(before);
        auto* session = connections_.session();
        const Uint64 now = SDL_GetTicksNS();
        const double elapsed = static_cast<double>(now - previousTime) / 1e9;
        previousTime = now;
        if (menu_.playing() && !session)
        {
            accumulator += std::min(elapsed, .1);
            world_.setPlayerInput(World::localPlayerId, inputGate_.filter(rawInput()));
            while (accumulator >= stepSeconds)
            {
                world_.update(static_cast<float>(stepSeconds));
                accumulator -= stepSeconds;
            }
        }
        else
            accumulator = 0;
        const bool sceneVisible = menu_.playing() || menu_.screen() == Screen::Paused;
        bool rendered = true;
        if (sceneVisible)
        {
            if (session && session->snapshot())
                rendered = renderSnapshot(renderer_, session->interpolated(), session->playerId()) &&
                           renderMatchHud(renderer_, *session->snapshot(), session->playerId());
            else
                rendered = renderScene(renderer_, world_, false);
        }
        if (!rendered || (!menu_.playing() && !renderMenu(renderer_, menu_)) || !SDL_RenderPresent(renderer_))
        {
            SDL_Log("Rendering failed: %s", SDL_GetError());
            return 1;
        }
        SDL_Delay(8); // Чаще опрашивать ввод/сеть; серверный шаг независим от окна.
    }
    return 0;
}
} // namespace cubeshot::client
