#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
namespace cubeshot::online {
struct ServiceConfig {
    std::string publicIp = "127.0.0.1";
    std::uint16_t apiPort = 8080, firstGamePort = 27020;
    unsigned arenas = 2;
    bool trustProxy = false;
    std::chrono::milliseconds leaseDuration{10000};
};
// poll/step are called by the one simulation thread. HTTP workers only enqueue requests.
class Service {
public:
    explicit Service(ServiceConfig config = {});
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    void poll();
    void step();
    std::uint16_t apiPort() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
