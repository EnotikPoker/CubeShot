#include "network/Discovery.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cubeshot::network;
int main() {
    try {
        using Clock = std::chrono::steady_clock;
        Discovery browser(false, 29716);
        if (!browser.available()) throw std::runtime_error("Discovery browser bind");
        {
            Discovery room(true, 29716);
            if (!room.available()) throw std::runtime_error("Discovery advertiser bind");
            room.advertise(29715, 1, true);
            const auto deadline = Clock::now() + std::chrono::seconds(3);
            while (browser.rooms().empty() && Clock::now() < deadline) {
                browser.poll(); room.poll(); browser.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (browser.rooms().empty() || browser.rooms()[0].players != 1 || !browser.rooms()[0].joinable)
                throw std::runtime_error("Discover room with occupancy and joinability");
            room.advertise(29715, 2, false);
            const auto updated = Clock::now() + std::chrono::seconds(3);
            while (browser.rooms()[0].joinable && Clock::now() < updated) {
                browser.poll(); room.poll(); browser.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            if (browser.rooms()[0].joinable || browser.rooms()[0].players != 2) throw std::runtime_error("Full room advertisement");
        }
        const auto expired = Clock::now() + std::chrono::milliseconds(6500);
        while (!browser.rooms().empty() && Clock::now() < expired) {
            browser.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (!browser.rooms().empty()) throw std::runtime_error("Stopped room expires after six seconds");
        std::cout << "Discovery and expiry passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
