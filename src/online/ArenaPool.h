#pragma once

#include "network/Server.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cubeshot::online
{
class ArenaPool
{
public:
    struct Room
    {
        std::string id;
        std::unique_ptr<network::Server> server;
    };

    using Admission =
        std::function<std::optional<network::Admission>(std::size_t, const std::string&, const std::string&)>;

    ArenaPool(std::uint16_t firstPort, unsigned count, Admission admission);
    const std::vector<Room>& rooms() const;
    void poll();
    void step();

private:
    std::vector<Room> rooms_;
};
} // namespace cubeshot::online
