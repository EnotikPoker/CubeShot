#include "client/ConnectionController.h"
#include "network/Server.h"
#include "online/Service.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace cubeshot;
using namespace cubeshot::client;
using namespace std::chrono_literals;

namespace
{
using Clock = std::chrono::steady_clock;

void expect(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

template <class Tick, class Condition> void until(Tick tick, Condition condition, const char* message)
{
    const auto deadline = Clock::now() + 5s;
    while (!condition() && Clock::now() < deadline)
    {
        tick();
        std::this_thread::sleep_for(1ms);
    }
    expect(condition(), message);
}

struct Participant
{
    ConnectionController connection;
    Menu menu;
    std::string nickname;

    Participant(const std::string& url, std::string name) : nickname(std::move(name))
    {
        connection.setDirectoryUrl(url);
        menu.open(Screen::Online);
        menu.setNickname(nickname);
    }

    void update()
    {
        connection.update(menu, nickname);
        if (auto* session = connection.session())
            session->control({});
    }

    void handle(MenuAction action)
    {
        connection.handle(action, menu, nickname);
    }
};

void rejectedInput()
{
    Participant player("", "Игрок");
    player.handle(MenuAction::QuickMatch);
    expect(!player.connection.session() && player.menu.screen() == Screen::Online,
           "Unconfigured online service does not start a connection");
    expect(!player.menu.message().empty(), "Unconfigured service explains the failure");
    player.menu.open(Screen::Address);
    player.menu.text("999.0.0.1");
    player.handle(MenuAction::JoinAddress);
    expect(!player.connection.session() && player.menu.screen() == Screen::Address,
           "Invalid LAN address keeps the editable address screen");
}

void onlineConnectionLifecycle()
{
    online::ServiceConfig config;
    config.apiPort = 0;
    config.firstGamePort = 29840;
    online::Service service(config);
    const std::string url = "http://127.0.0.1:" + std::to_string(service.apiPort());
    Participant host(url, "Хозяин"), guest(url, "Гость");
    auto tick = [&]
    {
        service.poll();
        host.update();
        guest.update();
        service.step();
    };

    host.handle(MenuAction::QuickMatch);
    expect(host.menu.screen() == Screen::Connecting, "Quick match opens connecting screen");
    // Process the HTTP reservation without letting the controller collect its reply yet.
    network::Directory inspector;
    inspector.list(url);
    bool reserved = false;
    auto retry = Clock::now();
    auto waitForReservation = [&]
    {
        service.poll();
        service.step();
        if (auto response = inspector.poll())
        {
            for (const auto& room : response->rooms)
                reserved = reserved || room.reserved != 0;
        }
        if (!reserved && !inspector.busy() && Clock::now() >= retry)
        {
            inspector.list(url);
            retry = Clock::now() + 250ms;
        }
    };
    until(
        waitForReservation, [&] { return reserved; }, "Reservation is created before its reply is collected");
    host.handle(MenuAction::CancelConnection);
    expect(!host.connection.session() && host.menu.screen() == Screen::OnlineBrowser,
           "Cancelling an in-flight reservation returns to online browser");

    inspector.list(url);
    bool released = false;
    retry = Clock::now();
    until(
        tick,
        [&]
        {
            if (auto response = inspector.poll())
            {
                released = response->error.empty() && response->rooms.size() == 2;
                for (const auto& room : response->rooms)
                    released = released && room.reserved == 0;
            }
            if (!released && !inspector.busy() && Clock::now() >= retry)
            {
                inspector.list(url);
                retry = Clock::now() + 250ms;
            }
            return released;
        },
        "Cancelled reservation eventually frees its slot");

    host.handle(MenuAction::QuickMatch);
    guest.handle(MenuAction::QuickMatch);
    until(
        tick,
        [&]
        {
            const auto* a = host.connection.session();
            const auto* b = guest.connection.session();
            return a && b && a->snapshot() && b->snapshot() && a->snapshot()->connected[0] &&
                   a->snapshot()->connected[1] && host.menu.screen() == Screen::Lobby &&
                   guest.menu.screen() == Screen::Lobby;
        },
        "Both connection controllers reach the same online lobby");
    expect(host.connection.session()->playerId() != guest.connection.session()->playerId(),
           "Controllers receive distinct player slots");
    host.handle(MenuAction::ToggleReady);
    guest.handle(MenuAction::ToggleReady);
    until(
        tick,
        [&]
        {
            return host.connection.session()->snapshot()->phase == MatchPhase::Countdown &&
                   guest.menu.playing();
        },
        "Ready actions begin the authoritative match");

    guest.handle(MenuAction::LeaveRoom);
    expect(!guest.connection.session() && guest.menu.screen() == Screen::OnlineBrowser,
           "Leaving an online match closes the connection and opens online browser");
    until(
        tick, [&] { return host.menu.screen() == Screen::Lobby; },
        "Remaining participant returns to lobby after peer leaves");
    host.menu.open(Screen::Main);
    host.handle(MenuAction::LeaveRoom);
    expect(!host.connection.session() && host.menu.screen() == Screen::Main,
           "Returning to main menu does not reopen a connection browser");
}

void lanConnectionLifecycle()
{
    network::Server server(29850);
    Participant player("", "LAN");
    player.menu.open(Screen::Lan);
    player.menu.open(Screen::Address);
    player.menu.text("127.0.0.1:29850");
    player.handle(MenuAction::JoinAddress);
    auto tick = [&]
    {
        server.poll();
        player.update();
        server.step();
    };
    until(
        tick, [&] { return player.menu.screen() == Screen::Lobby; },
        "Manual LAN address reaches lobby through the controller");
    player.handle(MenuAction::LeaveRoom);
    expect(!player.connection.session() && player.menu.screen() == Screen::Browser,
           "Leaving LAN returns to LAN browser");
    player.menu.open(Screen::Playing);
    player.handle(MenuAction::StartTest);
    expect(!player.connection.session(), "Test mode releases connection infrastructure");
}
} // namespace

int main()
{
    if (!SDL_Init(0))
        return 1;
    int result = 0;
    try
    {
        rejectedInput();
        onlineConnectionLifecycle();
        lanConnectionLifecycle();
        std::cout << "Connection controller lifecycle passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    SDL_Quit();
    return result;
}
