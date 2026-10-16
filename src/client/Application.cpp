#include "client/Application.h"
#include "client/MenuRenderer.h"
#include "client/SceneRenderer.h"
#include "network/Identity.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace cubeshot::client {
Application::~Application() {
    leaveRoom(); discovery_.reset();
    if (crosshair_) { SDL_SetCursor(SDL_GetDefaultCursor()); SDL_DestroyCursor(crosshair_); }
    SDL_DestroyRenderer(renderer_); SDL_DestroyWindow(window_);
    if (sdlInitialized_) SDL_Quit();
}
bool Application::initialize() {
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL initialization failed: %s", SDL_GetError()); return false; }
    sdlInitialized_ = true;
    profile_ = loadProfile();
    if (!directoryOverride_.empty()) profile_.directoryUrl = directoryOverride_;
    menu_.setNickname(profile_.nickname);
    if (!SDL_CreateWindowAndRenderer("CubeShot " CUBESHOT_VERSION, 960, 540, 0, &window_, &renderer_) ||
        !SDL_SetRenderLogicalPresentation(renderer_, 960, 540, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        SDL_Log("Window/renderer creation failed: %s", SDL_GetError()); return false;
    }
    crosshair_ = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    return crosshair_ && SDL_SetCursor(SDL_GetDefaultCursor());
}
Player Application::ownPlayer() const {
    if (network_ && network_->predictedPlayer()) return *network_->predictedPlayer();
    if (network_ && network_->snapshot()) {
        for (const auto& p : network_->snapshot()->players) if (p.id == network_->playerId()) return p;
    }
    return world_.players().front();
}
PlayerInput Application::rawInput() const { return readPlayerInput(window_, renderer_, ownPlayer()); }
void Application::leaveRoom() {
    directory_.cancel();
    directory_.release(profile_.directoryUrl,reservationToken_); reservationToken_.clear(); onlineSession_=false;
    if (network_) network_->disconnect();
    network_.reset(); host_.stop(); hosting_ = false; lastRound_ = 0;
}
void Application::screenChanged(Screen before) {
    if (before == menu_.screen()) return;
    if (before == Screen::Nickname) menu_.setNickname(profile_.nickname);
    world_.clearPlayerInput(World::localPlayerId);
    inputGate_.reset(rawInput());
    SDL_SetCursor(menu_.playing() ? crosshair_ : SDL_GetDefaultCursor());
    if (menu_.screen() == Screen::Address || menu_.screen() == Screen::Nickname) SDL_StartTextInput(window_);
    else if (before == Screen::Address || before == Screen::Nickname) SDL_StopTextInput(window_);
}
bool Application::handle(MenuAction action) {
    if (action == MenuAction::Quit) return false;
    if (action == MenuAction::SaveNickname) { profile_.nickname=menu_.nickname(); saveProfile(profile_); }
    if (action == MenuAction::FetchOnline) {
        directory_.list(profile_.directoryUrl); nextDirectoryRefresh_=SDL_GetTicks()+2000;
    }
    if (action == MenuAction::QuickMatch || action == MenuAction::JoinOnline) {
        if (!network::validDirectoryUrl(profile_.directoryUrl)) { menu_.setMessage(U"Интернет-сервис ещё не настроен"); return true; }
        leaveRoom(); discovery_.reset(); onlineSession_=true; profile_.nickname=menu_.nickname();
        if (!network::validNickname(profile_.nickname)) profile_.nickname="Игрок";
        directory_.reserve(profile_.directoryUrl,profile_.nickname,action==MenuAction::JoinOnline?menu_.selectedServer():std::string{});
        menu_.open(Screen::Connecting);
    }
    if (action == MenuAction::StartTest) { leaveRoom(); discovery_.reset(); world_ = World{}; }
    if (action == MenuAction::FindRooms) {
        discovery_ = std::make_unique<network::Discovery>(); menu_.setRooms({});
        if (!discovery_->available()) menu_.setMessage(U"Поиск недоступен. Используйте Ввести IP");
    }
    if (action == MenuAction::CancelConnection || action == MenuAction::LeaveRoom) {
        const bool toMain = menu_.screen() == Screen::Main;
        const bool online=onlineSession_ || menu_.online();
        leaveRoom();
        if (!toMain) {
            if (online) { menu_.open(Screen::OnlineBrowser); directory_.list(profile_.directoryUrl); nextDirectoryRefresh_=SDL_GetTicks()+2000; }
            else { menu_.open(Screen::Browser); discovery_ = std::make_unique<network::Discovery>(); menu_.setRooms({}); }
        }
    }
    if (action == MenuAction::ToggleReady && network_ && network_->snapshot()) {
        network_->ready(!network_->snapshot()->ready[network_->playerId() - 1]);
    }
    if (action == MenuAction::CreateRoom || action == MenuAction::JoinRoom || action == MenuAction::JoinAddress) {
        const std::string address = action == MenuAction::CreateRoom ? "127.0.0.1:27015" :
                                    action == MenuAction::JoinRoom ? menu_.selectedAddress() : menu_.address();
        if (!network::validAddress(address)) { menu_.setMessage(U"Некорректный IPv4 или порт"); return true; }
        leaveRoom(); discovery_.reset();
        try {
            hosting_ = action == MenuAction::CreateRoom;
            if (hosting_ && !host_.start()) throw std::runtime_error(host_.error());
            network_ = std::make_unique<network::Client>();
            if (!network_->connect(address, hosting_ ? host_.token() : "",profile_.nickname)) throw std::runtime_error(network_->error());
            menu_.open(Screen::Connecting);
        } catch (const std::exception& e) {
            SDL_Log("LAN startup failed: %s", e.what()); leaveRoom(); menu_.open(Screen::Lan);
            menu_.setMessage(U"Не удалось запустить комнату. Проверьте сервер и порт");
        }
    }
    return true;
}
void Application::updateDirectory() {
    if (auto result=directory_.poll()) {
        if (!result->error.empty()) {
            if (result->kind==network::DirectoryResult::Kind::Reserved && onlineSession_) { leaveRoom(); menu_.open(Screen::OnlineBrowser); }
            if (menu_.screen()==Screen::OnlineBrowser || menu_.screen()==Screen::Online) {
                const auto& error=result->error;
                menu_.setMessage(error=="not_configured"?U"Интернет-сервис ещё не настроен":
                    error=="no_free_servers"?U"Нет свободных мест. Повторите поиск позже":
                    error=="incompatible_protocol"?U"Несовместимая версия. Обновите игру":
                    error=="rate_limited"?U"Слишком много запросов. Подождите":U"Сервис недоступен. Повторите попытку");
            }
        } else if (result->kind==network::DirectoryResult::Kind::Rooms && menu_.screen()==Screen::OnlineBrowser) {
            menu_.setOnlineRooms(std::move(result->rooms)); menu_.setMessage({});
        } else if (result->kind==network::DirectoryResult::Kind::Reserved) {
            if (onlineSession_ && menu_.screen()==Screen::Connecting) {
                reservationToken_=result->token;
                network_=std::make_unique<network::Client>();
                if (!network_->connect(result->address,"",profile_.nickname,result->token)) {
                    leaveRoom(); menu_.open(Screen::OnlineBrowser); menu_.setMessage(U"Не удалось подключиться к серверу");
                }
            } else directory_.release(profile_.directoryUrl,result->token);
        }
    }
    if (menu_.screen()==Screen::OnlineBrowser && !directory_.busy() && SDL_GetTicks()>=nextDirectoryRefresh_) {
        directory_.list(profile_.directoryUrl); nextDirectoryRefresh_=SDL_GetTicks()+2000;
    }
}
void Application::updateNetwork() {
    updateDirectory();
    if (menu_.screen() == Screen::Browser) {
        if (!discovery_) discovery_ = std::make_unique<network::Discovery>();
        discovery_->poll(); std::vector<ListedRoom> rooms;
        for (const auto& r : discovery_->rooms()) rooms.push_back({r.address, r.players, r.joinable});
        menu_.setRooms(std::move(rooms));
    }
    if (!network_) return;
    network_->poll();
    if ((hosting_ && !host_.running()) || (!network_->connected() && !network_->connecting())) {
        const auto error = network_->error();
        const bool online=onlineSession_;
        SDL_Log("Network connection ended: %s", error.c_str());
        std::u32string message = online ? U"Соединение закрыто. Сервер недоступен" : U"Соединение закрыто. Хозяин вышел или сервер недоступен";
        if (error.find("protocol") != std::string::npos) message = U"Несовместимая версия игры на сервере";
        else if (error.find("Room full") != std::string::npos) message = U"Комната занята или матч уже начался";
        else if (error.find("Reservation") != std::string::npos) message = U"Бронирование истекло. Повторите подключение";
        else if (error.find("timed out") != std::string::npos) message = U"Сервер не ответил за 5 секунд";
        else if (hosting_) message = U"Сервер завершился. Проверьте свободен ли порт 27015";
        leaveRoom();
        if (online) { menu_.open(Screen::OnlineBrowser); nextDirectoryRefresh_=SDL_GetTicks()+2000; }
        else { menu_.open(Screen::Browser); discovery_ = std::make_unique<network::Discovery>(); menu_.setRooms({}); }
        menu_.setMessage(std::move(message)); return;
    }
    if (network_->connected() && network_->snapshot()) {
        const auto& s = *network_->snapshot();
        menu_.networkState(s, network_->playerId());
        if (lastRound_ != s.round) { lastRound_ = s.round; inputGate_.reset(rawInput()); }
        network_->control(menu_.playing() ? inputGate_.filter(rawInput()) : PlayerInput{0, false, false, ownPlayer().aimDirection});
    }
}
int Application::run() {
    if (!initialize()) return 1;
    constexpr double stepSeconds = 1.0 / 120;
    double accumulator = 0;
    Uint64 previousTime = SDL_GetTicksNS();
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) { running = false; break; }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) { menu_.cancelClick(); world_.clearPlayerInput(World::localPlayerId); }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) inputGate_.reset(rawInput());
            if ((SDL_GetWindowFlags(window_) & SDL_WINDOW_INPUT_FOCUS) == 0) continue;
            const Screen before = menu_.screen();
            MenuAction action = MenuAction::None;
            if (event.type == SDL_EVENT_TEXT_INPUT && (before == Screen::Address || before == Screen::Nickname)) menu_.text(event.text.text);
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                const auto key = event.key.scancode;
                if (key == SDL_SCANCODE_ESCAPE) {
                    if (before == Screen::Connecting) action = MenuAction::CancelConnection;
                    else if (before == Screen::Lobby || before == Screen::Result) action = MenuAction::LeaveRoom;
                    else menu_.escape();
                } else if (menu_.playing() && key == SDL_SCANCODE_R && !network_) {
                    world_ = World{}; inputGate_.reset(rawInput()); accumulator = 0;
                } else if (!menu_.playing()) {
                    if (key == SDL_SCANCODE_BACKSPACE && (before == Screen::Address || before == Screen::Nickname)) menu_.backspace();
                    if (key == SDL_SCANCODE_V && (event.key.mod & SDL_KMOD_CTRL) && (before == Screen::Address || before == Screen::Nickname)) {
                        char* text = SDL_GetClipboardText(); if (text) { menu_.text(text); SDL_free(text); }
                    }
                    if (key == SDL_SCANCODE_A && (event.key.mod & SDL_KMOD_CTRL) && before == Screen::Nickname) menu_.clearNickname();
                    if (key == SDL_SCANCODE_UP || (key == SDL_SCANCODE_W && before != Screen::Address && before != Screen::Nickname)) menu_.moveSelection(-1);
                    if (key == SDL_SCANCODE_DOWN || (key == SDL_SCANCODE_S && before != Screen::Address && before != Screen::Nickname)) menu_.moveSelection(1);
                    if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) action = menu_.activate();
                }
            }
            // Дискретные события сохраняют даже очень короткие клики/прыжки.
            const bool jumpEvent = (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                                   event.key.scancode == SDL_SCANCODE_SPACE && !event.key.repeat;
            const bool shotEvent = (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                                   event.button.button == SDL_BUTTON_LEFT;
            if (menu_.playing() && (jumpEvent || shotEvent)) {
                auto input = rawInput();
                if (jumpEvent) input.jump = event.type == SDL_EVENT_KEY_DOWN;
                if (shotEvent) {
                    input.shoot = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    Vec2 logical;
                    if (SDL_RenderCoordinatesFromWindow(renderer_, event.button.x, event.button.y, &logical.x, &logical.y)) {
                        const auto origin = ownPlayer().weaponOrigin();
                        const Vec2 aim{logical.x - origin.x, logical.y - origin.y};
                        if (std::hypot(aim.x, aim.y) > 0.0001f) input.aimDirection = aim;
                    }
                }
                input = inputGate_.filter(input);
                if (network_) {
                    if (jumpEvent && input.jump) network_->action(true, false, input.aimDirection);
                    if (shotEvent && input.shoot) network_->action(false, true, input.aimDirection);
                } else world_.setPlayerInput(World::localPlayerId, input);
            } else if (shotEvent && !menu_.playing()) {
                Vec2 logical;
                if (SDL_RenderCoordinatesFromWindow(renderer_, event.button.x, event.button.y, &logical.x, &logical.y)) {
                    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) menu_.press(logical);
                    else action = menu_.release(logical);
                }
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && !menu_.playing()) {
                Vec2 logical;
                if (SDL_RenderCoordinatesFromWindow(renderer_, event.motion.x, event.motion.y, &logical.x, &logical.y)) menu_.hover(logical);
            }
            running = handle(action);
            screenChanged(before);
            if (before != menu_.screen()) { accumulator = 0; previousTime = SDL_GetTicksNS(); }
            if (!running) break;
        }
        if (!running) break;
        const Screen before = menu_.screen();
        updateNetwork(); screenChanged(before);
        const Uint64 now = SDL_GetTicksNS();
        const double elapsed = static_cast<double>(now - previousTime) / 1e9; previousTime = now;
        if (menu_.playing() && !network_) {
            accumulator += std::min(elapsed, .1);
            world_.setPlayerInput(World::localPlayerId, inputGate_.filter(rawInput()));
            while (accumulator >= stepSeconds) { world_.update(static_cast<float>(stepSeconds)); accumulator -= stepSeconds; }
        } else accumulator = 0;
        const bool sceneVisible = menu_.playing() || menu_.screen() == Screen::Paused;
        bool rendered = true;
        if (sceneVisible) {
            if (network_ && network_->snapshot()) rendered = renderSnapshot(renderer_, network_->interpolated(), network_->playerId()) &&
                renderMatchHud(renderer_, *network_->snapshot(), network_->playerId());
            else rendered = renderScene(renderer_, world_, false);
        }
        if (!rendered || (!menu_.playing() && !renderMenu(renderer_, menu_)) || !SDL_RenderPresent(renderer_)) {
            SDL_Log("Rendering failed: %s", SDL_GetError()); return 1;
        }
        SDL_Delay(8); // Чаще опрашивать ввод/сеть; серверный шаг независим от окна.
    }
    return 0;
}
}
