#include "network/Discovery.h"
#include "network/Server.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace
{
volatile std::sig_atomic_t running = 1;

void stop(int)
{
    running = 0;
}
} // namespace

int main(int argc, char** argv)
{
    std::uint16_t port = cubeshot::network::gamePort;
    std::string token;
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--version")
            {
                std::cout << "CubeShot " CUBESHOT_VERSION "\n";
                return 0;
            }
            if (arg == "--help")
            {
                std::cout << "cubeshot_server [--port 27015] [--owner-token TOKEN]\n";
                return 0;
            }
            if (arg == "--port" && i + 1 < argc)
            {
                const std::string value = argv[++i];
                std::size_t used = 0;
                const auto n = std::stoul(value, &used);
                if (used != value.size() || !n || n > 65535)
                    throw std::runtime_error("Invalid port");
                port = static_cast<std::uint16_t>(n);
            }
            else if (arg == "--owner-token" && i + 1 < argc)
                token = argv[++i];
            else
                throw std::runtime_error("Unknown or incomplete argument: " + arg);
        }
        cubeshot::network::Server server(port, token);
        cubeshot::network::Discovery discovery(true);
        std::signal(SIGINT, stop);
        std::signal(SIGTERM, stop);
        std::cout << "CubeShot " CUBESHOT_VERSION " LAN server, UDP " << server.port() << '\n' << std::flush;
        if (!discovery.available())
            std::cerr << "Discovery unavailable; use manual IPv4 connection\n";
        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();
        const auto started = previous;
        double accumulator = 0;
        bool ownerJoined = token.empty();
        while (running && !server.hostLeft())
        {
            server.poll();
            const auto snapshot = server.match().snapshot();
            if (snapshot.connected[0])
                ownerJoined = true;
            if (!ownerJoined && Clock::now() - started > std::chrono::seconds(10))
                break;
            const unsigned count = snapshot.connected[0] + snapshot.connected[1];
            discovery.advertise(server.port(), count,
                                count < 2 && snapshot.phase == cubeshot::MatchPhase::Lobby && ownerJoined &&
                                    !server.hostLeft());
            discovery.poll();
            const auto now = Clock::now();
            accumulator += std::min(std::chrono::duration<double>(now - previous).count(), .1);
            previous = now;
            while (accumulator >= 1.0 / 120)
            {
                server.step();
                accumulator -= 1.0 / 120;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
