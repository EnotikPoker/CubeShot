#pragma once

#include <cstdint>
#include <memory>

namespace cubeshot::online
{
struct ServiceConfig;
class ArenaPool;
class Reservations;

// HTTP workers enqueue requests; poll() executes them on the simulation thread.
class HttpApi
{
public:
    HttpApi(ServiceConfig config, const ArenaPool& pool, Reservations& reservations);
    ~HttpApi();
    HttpApi(const HttpApi&) = delete;
    HttpApi& operator=(const HttpApi&) = delete;
    void poll();
    std::uint16_t port() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cubeshot::online
