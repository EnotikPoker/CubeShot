#include "client/SoundScene.h"
#include "network/Client.h"
#include "network/NetworkSimulation.h"
#include "network/Server.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cubeshot;
using namespace cubeshot::client;
using namespace cubeshot::network;
using Clock = std::chrono::steady_clock;

void expect(bool v, const char* text)
{
    if (!v)
        throw std::runtime_error(text);
}

int main()
{
    try
    {
        expect(SDL_Init(0), "Initialize SDL for audio bank");
        AudioMixer mixer;
        expect(mixer.load(std::filesystem::u8path(SDL_GetBasePath()) / "assets" / "audio"),
               "Load sound bank");
        SoundScene sounds(mixer);
        Server server(29910, "audio-owner");
        Client host, guest;
        simulateNetwork(5, 80, 15);
        auto previous = Clock::now();
        double accumulator = 0;
        bool predicted = false, confirmed = false;
        auto tick = [&]
        {
            server.poll();
            host.poll();
            guest.poll();
            auto now = Clock::now();
            float dt = std::chrono::duration<float>(now - previous).count();
            previous = now;
            accumulator += dt;
            while (accumulator >= 1.0 / 120)
            {
                server.step();
                accumulator -= 1.0 / 120;
            }
            PlayerInput input;
            input.aimDirection = {0, -1};
            host.control(input);
            guest.control({});
            auto events = host.takeEvents();
            for (auto e : events)
                if (e.kind == EventKind::Shot && e.actor == 1)
                {
                    if (e.id)
                        confirmed = true;
                    else
                        predicted = true;
                }
            if (host.snapshot())
            {
                const auto& s = *host.snapshot();
                auto players = s.players;
                if (host.predictedPlayer())
                    players[0] = *host.predictedPlayer();
                sounds.update(dt, players, server.match().world().arena(), events, 1, s.entities, s.bullets);
            }
            // Sound selection is tested without relying on an actual playback device.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        };
        auto until = [&](auto condition, double seconds, const char* why)
        {
            auto deadline = Clock::now() + std::chrono::duration<double>(seconds);
            while (!condition() && Clock::now() < deadline)
                tick();
            expect(condition(), why);
        };
        auto address = "127.0.0.1:" + std::to_string(server.port());
        host.connect(address, "audio-owner");
        until([&] { return host.connected() && host.snapshot().has_value(); }, 5,
              "Owner connects under lag/loss");
        guest.connect(address);
        until([&] { return guest.connected() && guest.snapshot().has_value(); }, 5,
              "Guest connects under lag/loss");
        until([&] { return host.snapshot()->connected[1]; }, 3, "Both participants observed");
        host.ready(true);
        guest.ready(true);
        until([&] { return host.snapshot()->phase == MatchPhase::Round; }, 6, "Round starts");
        sounds.clear();
        mixer.clear();
        auto before = mixer.starts();
        host.action(false, true, {0, -1});
        until([&] { return predicted; }, .3, "Local prediction supplies immediate shot sound");
        expect(!confirmed && mixer.starts() == before + 1, "Own shot sounds before network confirmation");
        until([&] { return confirmed; }, 3, "Authoritative shot arrives under loss");
        auto deadline = Clock::now() + std::chrono::milliseconds(400);
        while (Clock::now() < deadline)
            tick();
        expect(mixer.starts() == before + 1, "Network confirmation does not repeat local shot");
        simulateNetwork(0, 0);
        std::cout << "Audio predicted/server event integration passed with 5% loss and 80ms lag\n";
    }
    catch (const std::exception& e)
    {
        simulateNetwork(0, 0);
        std::cerr << e.what() << '\n';
        SDL_Quit();
        return 1;
    }
    SDL_Quit();
}
