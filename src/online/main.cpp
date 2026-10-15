#include "online/Service.h"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
namespace { volatile std::sig_atomic_t running=1; void stop(int) { running=0; } }
int main(int argc,char** argv) {
    try {
        cubeshot::online::ServiceConfig config;
        for (int i=1;i<argc;++i) {
            const std::string option=argv[i];
            if (option=="--help") { std::cout << "cubeshot_online_server [--public-ip IPv4] [--api-port 8080] [--first-port 27020] [--arenas 2] [--trust-proxy]\n"; return 0; }
            if (option=="--trust-proxy") { config.trustProxy=true; continue; }
            if (i+1==argc) throw std::runtime_error("Missing option value");
            const std::string value=argv[++i];
            if (option=="--public-ip") { config.publicIp=value; continue; }
            std::size_t used=0; const auto number=std::stoul(value,&used);
            if (used!=value.size() || !number || number>65535) throw std::runtime_error("Invalid option value");
            if (option=="--api-port") config.apiPort=static_cast<std::uint16_t>(number);
            else if (option=="--first-port") config.firstGamePort=static_cast<std::uint16_t>(number);
            else if (option=="--arenas") config.arenas=static_cast<unsigned>(number);
            else throw std::runtime_error("Unknown option");
        }
        cubeshot::online::Service service(config);
        std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
        std::cout << "CubeShot " CUBESHOT_VERSION " online: " << config.arenas << " arenas, API 127.0.0.1:" << service.apiPort() << '\n' << std::flush;
        using Clock=std::chrono::steady_clock; auto previous=Clock::now(); double accumulator=0;
        while (running) {
            service.poll(); const auto now=Clock::now();
            accumulator+=std::min(.1,std::chrono::duration<double>(now-previous).count()); previous=now;
            while (accumulator>=1.0/120) { service.step(); accumulator-=1.0/120; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
