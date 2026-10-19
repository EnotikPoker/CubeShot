#include "online/ArenaPool.h"

namespace cubeshot::online
{
ArenaPool::ArenaPool(std::uint16_t firstPort, unsigned count, Admission admission)
{
    for (unsigned i = 0; i < count; ++i)
    {
        auto admit = [admission, i](const std::string& token, const std::string& name)
        { return admission(i, token, name); };
        const auto port = static_cast<std::uint16_t>(firstPort + i);
        rooms_.push_back(
            {"duel-" + std::to_string(i + 1), std::make_unique<network::Server>(port, "", admit)});
    }
}

const std::vector<ArenaPool::Room>& ArenaPool::rooms() const
{
    return rooms_;
}

void ArenaPool::poll()
{
    for (auto& room : rooms_)
        room.server->poll();
}

void ArenaPool::step()
{
    for (auto& room : rooms_)
        room.server->step();
}
} // namespace cubeshot::online
