#pragma once
#include "network/Protocol.h"
#include <memory>
#include <vector>
namespace cubeshot::network {
struct Room { std::string address, name; unsigned players = 0; bool joinable = false; };
class Discovery {
public:
    // advertiser=true: отвечать на запросы; иначе искать комнаты.
    explicit Discovery(bool advertiser = false, std::uint16_t port = discoveryPort);
    ~Discovery();
    Discovery(const Discovery&) = delete;
    Discovery& operator=(const Discovery&) = delete;
    bool available() const;
    void advertise(std::uint16_t gamePort, unsigned players, bool joinable);
    void poll();
    std::vector<Room> rooms() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
