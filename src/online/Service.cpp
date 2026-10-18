#include "online/Service.h"
#include "network/Address.h"
#include "online/ArenaPool.h"
#include "online/HttpApi.h"
#include "online/Reservations.h"
#include <stdexcept>
#include <utility>

namespace cubeshot::online
{
struct Service::Impl
{
    // Destruction order matters: stop HTTP workers before destroying their domain state.
    std::unique_ptr<ArenaPool> pool;
    std::unique_ptr<Reservations> reservations;
    std::unique_ptr<HttpApi> api;

    explicit Impl(ServiceConfig config)
    {
        if (!network::validAddress(config.publicIp) || config.publicIp.find(':') != std::string::npos ||
            !config.firstGamePort || !config.arenas || config.arenas > 16 ||
            unsigned(config.firstGamePort) + config.arenas > 65536)
            throw std::runtime_error("Invalid public IPv4, arena count or port range");

        // The callback runs only when poll() starts, after Reservations has been constructed.
        auto admit = [this](std::size_t room, const std::string& token, const std::string& name)
        { return reservations->admit(room, token, name); };
        pool = std::make_unique<ArenaPool>(config.firstGamePort, config.arenas, admit);
        reservations = std::make_unique<Reservations>(*pool, config.leaseDuration);
        api = std::make_unique<HttpApi>(std::move(config), *pool, *reservations);
    }
};

Service::Service(ServiceConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}

Service::~Service() = default;

void Service::poll()
{
    impl_->pool->poll();
    impl_->api->poll();
}

void Service::step()
{
    impl_->pool->step();
}

std::uint16_t Service::apiPort() const
{
    return impl_->api->port();
}
} // namespace cubeshot::online
