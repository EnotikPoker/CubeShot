#include "client/HostProcess.h"
#include "network/Client.h"
#include "network/Server.h"
#include <SDL3/SDL.h>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;
using namespace cubeshot::client;
using namespace cubeshot::network;
int main() {
    if (!SDL_Init(0)) return 1;
    int result = 0;
    try {
        HostProcess host;
        if (!host.start()) throw std::runtime_error(host.error());
        Client client;
        client.connect("127.0.0.1:27015", host.token());
        const auto deadline = SDL_GetTicks() + 5000;
        while (!client.connected() && client.connecting() && SDL_GetTicks() < deadline) { client.poll(); SDL_Delay(1); }
        if (!client.connected() || client.playerId() != 1 || !host.running()) throw std::runtime_error("SDL child server accepts owner");
        client.disconnect(); host.stop();
        if (host.running()) throw std::runtime_error("Child was not stopped and reaped");
        // Verify that the child released the same UDP port, then occupied-port handling.
        Server occupied(27015);
        if (!host.start()) throw std::runtime_error("Start child process even if server port occupied");
        const auto exitDeadline = SDL_GetTicks() + 3000;
        while (host.running() && SDL_GetTicks() < exitDeadline) SDL_Delay(5);
        if (host.running()) throw std::runtime_error("Child exits on occupied port");
        std::cout << "Host process lifecycle passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    SDL_Quit(); return result;
}
