#include "network/Client.h"
#include "network/NetworkSimulation.h"
#include "network/Server.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cubeshot;
using namespace cubeshot::network;
using Clock = std::chrono::steady_clock;

void check(bool x, const char* t)
{
    if (!x)
        throw std::runtime_error(t);
}

void run(MatchFormat format, unsigned count, unsigned port)
{
    Server server(port, "four-owner");
    std::array<Client, 4> clients;
    auto tick = [&]
    {
        server.poll();
        for (auto& c : clients)
            c.poll();
        server.step();
        for (auto& c : clients)
            if (c.connected())
                c.control({});
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    };
    auto until = [&](auto condition, float seconds, const char* why)
    {
        auto end = Clock::now() + std::chrono::duration<double>(seconds);
        while (!condition() && Clock::now() < end)
            tick();
        check(condition(), why);
    };
    const auto address = "127.0.0.1:" + std::to_string(server.port());
    clients[0].connect(address, "four-owner");
    until([&] { return clients[0].connected() && clients[0].snapshot().has_value(); }, 4, "Host joins");
    MatchRules rules;
    rules.format = format;
    rules.participants = count;
    clients[0].selectRules(rules);
    until([&] { return clients[0].snapshot()->rules.participants == count; }, 3, "Capacity reaches host");
    for (unsigned i = 1; i < count; ++i)
    {
        clients[i].connect(address);
        until([&] { return clients[i].connected() && clients[i].snapshot().has_value(); }, 4,
              "Each additional client joins");
    }
    until(
        [&]
        {
            for (unsigned i = 0; i < count; ++i)
                if (!clients[i].connected() || !clients[i].snapshot() ||
                    !clients[i].snapshot()->connected[count - 1])
                    return false;
            return true;
        },
        3, "Every client sees complete roster");
    for (unsigned i = 0; i < count; ++i)
        check(clients[i].playerId() == i + 1, "Stable distinct participant IDs");
    clients[count - 1].selectRules(MatchRules{});
    for (unsigned i = 0; i < 80; ++i)
        tick();
    check(server.match().snapshot().rules.participants == count, "Guest cannot change capacity");
    for (unsigned i = 0; i < count; ++i)
        clients[i].ready(true);
    until(
        [&]
        {
            for (unsigned i = 0; i < count; ++i)
                if (clients[i].snapshot()->phase != MatchPhase::Round)
                    return false;
            return true;
        },
        5, "All clients start authoritative round");
    clients[count - 1].action(false, true, {0, -1});
    until([&] { return server.match().world().players()[count - 1].weapon.shots > 0; }, 3,
          "Fourth player action reaches server");
    until([&] { return clients[count - 1].snapshot()->actionAck[count - 1] > 0; }, 3,
          "Fourth player receives independent action acknowledgement");
    check(clients[count - 1].predictedPlayer().has_value(), "Fourth player predicts movement");
    auto life = server.match().world().players()[1].lifeRevision;
    clients[1].disconnect();
    until([&] { return clients[0].snapshot()->slots[1] == SlotKind::Bot; }, 3, "Peer becomes bot");
    check(server.match().world().players()[1].lifeRevision == life &&
              server.match().phase() != MatchPhase::Lobby,
          "Replacement does not recreate body or reset match");
    Client late;
    late.connect(address);
    until(
        [&]
        {
            late.poll();
            return !late.connecting();
        },
        3, "Late admission resolves");
    check(!late.connected(), "Started match cannot admit new humans");
}

void serverBots()
{
    Server server(29944, "bot-owner");
    Client owner;
    auto tick = [&]
    {
        server.poll();
        owner.poll();
        server.step();
        owner.control({});
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    };
    auto until = [&](auto f)
    {
        for (unsigned n = 0; n < 2000 && !f(); ++n)
            tick();
        check(f(), "Server bot configuration/round completion");
    };
    owner.connect("127.0.0.1:29944", "bot-owner");
    until([&] { return owner.snapshot().has_value(); });
    auto r = MatchRules{};
    r.format = MatchFormat::Teams;
    r.participants = 4;
    owner.selectRules(r);
    until([&] { return owner.snapshot()->rules.participants == 4; });
    for (unsigned id = 2; id <= 4; ++id)
    {
        owner.configureSlot(id, SlotKind::Bot, 1, id % 2 ? 1 : 2);
        until([&] { return owner.snapshot()->slots[id - 1] == SlotKind::Bot; });
    }
    owner.ready(true);
    until([&] { return owner.snapshot()->phase == MatchPhase::Round; });
    for (unsigned n = 0; n < 240; ++n)
        tick();
    unsigned shots = 0;
    for (unsigned i = 1; i < 4; ++i)
        shots += server.match().world().players()[i].weapon.shots;
    check(shots > 0, "Server-owned bots participate in combat");
}

int main()
{
    try
    {
        simulateNetwork(3, 25, 5);
        run(MatchFormat::FreeForAll, 3, 29940);
        run(MatchFormat::FreeForAll, 4, 29941);
        run(MatchFormat::Teams, 4, 29942);
        simulateNetwork(0, 0);
        serverBots();
        std::cout << "Four-client actions, prediction, replacement and server bots passed\n";
    }
    catch (const std::exception& e)
    {
        simulateNetwork(0, 0);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
