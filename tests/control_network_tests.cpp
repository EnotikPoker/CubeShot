#include "network/Client.h"
#include "network/NetworkSimulation.h"
#include "network/Server.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cubeshot;
using namespace cubeshot::network;
using Clock = std::chrono::steady_clock;

void expect(bool b, const char* why)
{
    if (!b)
        throw std::runtime_error(why);
}

int main()
{
    try
    {
        simulateNetwork(5, 80, 15);
        Server server(29732, "owner");
        Client host, guest;
        PlayerInput hostInput;
        host.connect("127.0.0.1:29732", "owner");
        auto previous = Clock::now();
        double accumulator = 0;
        auto tick = [&]
        {
            server.poll();
            host.poll();
            guest.poll();
            host.control(hostInput);
            guest.control({});
            auto now = Clock::now();
            accumulator += std::chrono::duration<double>(now - previous).count();
            previous = now;
            while (accumulator >= 1.0 / 120)
            {
                server.step();
                accumulator -= 1.0 / 120;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
        auto until = [&](auto predicate, double seconds, const char* why)
        {
            auto deadline = Clock::now() + std::chrono::duration<double>(seconds);
            while (!predicate() && Clock::now() < deadline)
                tick();
            expect(predicate(), why);
        };
        until([&] { return host.connected() && host.snapshot(); }, 4, "Owner connects with impairment");
        guest.connect("127.0.0.1:29732");
        until([&] { return guest.connected() && guest.snapshot(); }, 4, "Guest connects with impairment");
        MatchRules rules;
        rules.mode = GameMode::Control;
        rules.controlGoal = 120;
        host.selectRules(rules);
        until(
            [&]
            {
                return host.snapshot()->rules.mode == GameMode::Control &&
                       guest.snapshot()->rules.mode == GameMode::Control;
            },
            3, "Rules agree on both clients");
        host.ready(true);
        guest.ready(true);
        until(
            [&] {
                return host.snapshot()->phase == MatchPhase::Round &&
                       guest.snapshot()->phase == MatchPhase::Round;
            },
            5, "Control starts normally");
        // Stage the close-range encounter; the lethal action is delivered by the real host connection.
        auto& w = const_cast<World&>(server.match().world());
        auto& p = const_cast<Player&>(w.players()[0]);
        auto& q = const_cast<Player&>(w.players()[1]);
        p.position = {100, 392};
        q.position = {200, 392};
        w.setHealth(2, 1);
        host.action(false, true, {1, 0});
        until(
            [&] {
                return guest.snapshot()->mode.respawn[1].choosing &&
                       host.snapshot()->mode.respawn[1].choosing;
            },
            3, "Death and personal upgrade agree under packet loss");
        auto elapsed = guest.snapshot()->mode.elapsed;
        guest.chooseUpgrade(UpgradeKind::ActiveQ);
        until([&] { return guest.snapshot()->weaponChoice[1] == WeaponChoice::Spinning; }, 3,
              "Personal roll reliably starts without pausing server");
        auto chosen = guest.snapshot()->rolledActive[1];
        hostInput.moveDirection = 1;
        until(
            [&]
            {
                return guest.snapshot()->players[1].alive() &&
                       guest.snapshot()->players[1].lifeRevision == 1 &&
                       host.snapshot()->players[1].lifeRevision == 1;
            },
            11, "Independent respawn reaches both clients");
        expect(guest.snapshot()->phase == MatchPhase::Round && guest.snapshot()->mode.elapsed > elapsed + 7,
               "Survivor's simulation remains live throughout roulette");
        expect(host.snapshot()->players[1].actives[0].kind == chosen &&
                   guest.snapshot()->players[1].actives[0].kind == chosen,
               "New active loadout agrees after respawn");
        guest.action(false, false, {-1, 0}, false, 1);
        until(
            [&]
            {
                return server.match().world().players()[1].activeSerial > 0 || chosen == ActiveKind::Heal ||
                       chosen == ActiveKind::Screen || chosen == ActiveKind::Mine;
            },
            3, "Actions operate in the new life");
        expect(host.connected() && guest.connected(),
               "Clients stay connected after continuous-mode death and upgrades");
        simulateNetwork(0, 0);
        std::cout << "Continuous control with packet loss passed\n";
    }
    catch (const std::exception& e)
    {
        simulateNetwork(0, 0);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
