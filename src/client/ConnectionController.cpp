#include "client/ConnectionController.h"
#include "network/Address.h"
#include "network/Identity.h"
#include <SDL3/SDL.h>
#include <stdexcept>
#include <utility>

namespace cubeshot::client
{
ConnectionController::~ConnectionController()
{
    stop();
}

void ConnectionController::setDirectoryUrl(std::string url)
{
    directoryUrl_ = std::move(url);
}

network::Client* ConnectionController::session()
{
    return client_.get();
}

const network::Client* ConnectionController::session() const
{
    return client_.get();
}

void ConnectionController::stop()
{
    directory_.cancel();
    directory_.release(directoryUrl_, reservationToken_);
    reservationToken_.clear();
    onlineSession_ = false;
    if (client_)
        client_->disconnect();
    client_.reset();
    host_.stop();
    hosting_ = false;
    discovery_.reset();
}

void ConnectionController::handle(MenuAction action, Menu& menu, std::string& nickname)
{
    if (action == MenuAction::FetchOnline)
    {
        directory_.list(directoryUrl_);
        nextDirectoryRefresh_ = SDL_GetTicks() + 2000;
    }
    if (action == MenuAction::QuickMatch || action == MenuAction::JoinOnline)
    {
        if (!network::validDirectoryUrl(directoryUrl_))
        {
            menu.setMessage(U"Интернет-сервис ещё не настроен");
            return;
        }
        stop();
        onlineSession_ = true;
        nickname = menu.nickname();
        if (!network::validNickname(nickname))
            nickname = "Игрок";
        directory_.reserve(directoryUrl_, nickname,
                           action == MenuAction::JoinOnline ? menu.selectedServer() : std::string{});
        menu.open(Screen::Connecting);
    }
    if (action == MenuAction::StartTest)
    {
        stop();
    }
    if (action == MenuAction::FindRooms)
    {
        discovery_ = std::make_unique<network::Discovery>();
        menu.setRooms({});
        if (!discovery_->available())
            menu.setMessage(U"Поиск недоступен. Используйте Ввести IP");
    }
    if (action == MenuAction::CancelConnection || action == MenuAction::LeaveRoom)
    {
        const bool toMain = menu.screen() == Screen::Main;
        const bool online = onlineSession_ || menu.online();
        stop();
        if (!toMain)
        {
            if (online)
            {
                menu.open(Screen::OnlineBrowser);
                directory_.list(directoryUrl_);
                nextDirectoryRefresh_ = SDL_GetTicks() + 2000;
            }
            else
            {
                menu.open(Screen::Browser);
                discovery_ = std::make_unique<network::Discovery>();
                menu.setRooms({});
            }
        }
    }
    if (action == MenuAction::ToggleReady && client_ && client_->snapshot())
    {
        client_->ready(!client_->snapshot()->ready[client_->playerId() - 1]);
    }
    if (action == MenuAction::CreateRoom || action == MenuAction::JoinRoom ||
        action == MenuAction::JoinAddress)
    {
        const std::string address = action == MenuAction::CreateRoom ? "127.0.0.1:27015"
                                    : action == MenuAction::JoinRoom ? menu.selectedAddress()
                                                                     : menu.address();
        if (!network::validAddress(address))
        {
            menu.setMessage(U"Некорректный IPv4 или порт");
            return;
        }
        stop();
        try
        {
            hosting_ = action == MenuAction::CreateRoom;
            if (hosting_ && !host_.start())
                throw std::runtime_error(host_.error());
            client_ = std::make_unique<network::Client>();
            if (!client_->connect(address, hosting_ ? host_.token() : "", nickname))
                throw std::runtime_error(client_->error());
            menu.open(Screen::Connecting);
        }
        catch (const std::exception& e)
        {
            SDL_Log("LAN startup failed: %s", e.what());
            stop();
            menu.open(Screen::Lan);
            menu.setMessage(U"Не удалось запустить комнату. Проверьте сервер и порт");
        }
    }
}

void ConnectionController::updateDirectory(Menu& menu, const std::string& nickname)
{
    if (auto result = directory_.poll())
    {
        if (!result->error.empty())
        {
            if (result->kind == network::DirectoryResult::Kind::Reserved && onlineSession_)
            {
                stop();
                menu.open(Screen::OnlineBrowser);
            }
            if (menu.screen() == Screen::OnlineBrowser || menu.screen() == Screen::Online)
            {
                const auto& error = result->error;
                menu.setMessage(error == "not_configured"    ? U"Интернет-сервис ещё не настроен"
                                : error == "no_free_servers" ? U"Нет свободных мест. Повторите поиск позже"
                                : error == "incompatible_protocol" ? U"Несовместимая версия. Обновите игру"
                                : error == "rate_limited"          ? U"Слишком много запросов. Подождите"
                                                                   : U"Сервис недоступен. Повторите попытку");
            }
        }
        else if (result->kind == network::DirectoryResult::Kind::Rooms &&
                 menu.screen() == Screen::OnlineBrowser)
        {
            menu.setOnlineRooms(std::move(result->rooms));
            menu.setMessage({});
        }
        else if (result->kind == network::DirectoryResult::Kind::Reserved)
        {
            if (onlineSession_ && menu.screen() == Screen::Connecting)
            {
                reservationToken_ = result->token;
                client_ = std::make_unique<network::Client>();
                if (!client_->connect(result->address, "", nickname, result->token))
                {
                    stop();
                    menu.open(Screen::OnlineBrowser);
                    menu.setMessage(U"Не удалось подключиться к серверу");
                }
            }
            else
                directory_.release(directoryUrl_, result->token);
        }
    }
    if (menu.screen() == Screen::OnlineBrowser && !directory_.busy() &&
        SDL_GetTicks() >= nextDirectoryRefresh_)
    {
        directory_.list(directoryUrl_);
        nextDirectoryRefresh_ = SDL_GetTicks() + 2000;
    }
}

void ConnectionController::update(Menu& menu, const std::string& nickname)
{
    updateDirectory(menu, nickname);
    if (menu.screen() == Screen::Browser)
    {
        if (!discovery_)
            discovery_ = std::make_unique<network::Discovery>();
        discovery_->poll();
        std::vector<ListedRoom> rooms;
        for (const auto& r : discovery_->rooms())
            rooms.push_back({r.address, r.players, r.joinable});
        menu.setRooms(std::move(rooms));
    }
    if (!client_)
        return;
    client_->poll();
    if ((hosting_ && !host_.running()) || (!client_->connected() && !client_->connecting()))
    {
        const auto error = client_->error();
        const bool online = onlineSession_;
        SDL_Log("Network connection ended: %s", error.c_str());
        std::u32string message = online ? U"Соединение закрыто. Сервер недоступен"
                                        : U"Соединение закрыто. Хозяин вышел или сервер недоступен";
        if (error.find("protocol") != std::string::npos)
            message = U"Несовместимая версия игры на сервере";
        else if (error.find("Room full") != std::string::npos)
            message = U"Комната занята или матч уже начался";
        else if (error.find("Reservation") != std::string::npos)
            message = U"Бронирование истекло. Повторите подключение";
        else if (error.find("timed out") != std::string::npos)
            message = U"Сервер не ответил за 5 секунд";
        else if (hosting_)
            message = U"Сервер завершился. Проверьте свободен ли порт 27015";
        stop();
        if (online)
        {
            menu.open(Screen::OnlineBrowser);
            nextDirectoryRefresh_ = SDL_GetTicks() + 2000;
        }
        else
        {
            menu.open(Screen::Browser);
            discovery_ = std::make_unique<network::Discovery>();
            menu.setRooms({});
        }
        menu.setMessage(std::move(message));
        return;
    }
    if (client_->connected() && client_->snapshot())
    {
        const auto& s = *client_->snapshot();
        menu.networkState(s, client_->playerId());
    }
}
} // namespace cubeshot::client
