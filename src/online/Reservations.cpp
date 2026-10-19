#include "online/Reservations.h"
#include "network/Identity.h"
#include "online/ArenaPool.h"

namespace cubeshot::online
{
Reservations::Reservations(const ArenaPool& pool, std::chrono::milliseconds duration)
    : pool_(pool), duration_(duration)
{
}

void Reservations::expire()
{
    const auto now = Clock::now();
    for (auto it = leases_.begin(); it != leases_.end();)
    {
        if (it->second.expires <= now)
            it = leases_.erase(it);
        else
            ++it;
    }
}

std::array<bool, 2> Reservations::occupied(std::size_t room) const
{
    auto slots = pool_.rooms()[room].server->match().snapshot().connected;
    for (const auto& entry : leases_)
    {
        const auto& lease = entry.second;
        if (lease.room == room && lease.state == State::Pending)
            slots[lease.player - 1] = true;
    }
    return slots;
}

Reservations::Result Reservations::reserve(const std::string& ip, const std::string& nickname,
                                           const std::string& request, const std::string& server)
{
    expire();
    const auto& rooms = pool_.rooms();
    unsigned pending = 0;
    for (const auto& entry : leases_)
    {
        const auto& lease = entry.second;
        if (lease.ip == ip && lease.request == request)
        {
            if (lease.nickname != nickname || (!server.empty() && rooms[lease.room].id != server))
                return {nullptr, Error::Conflict};
            if (lease.state == State::Cancelled)
                return {nullptr, Error::Cancelled};
            return {&lease};
        }
        if (lease.ip == ip && lease.state == State::Pending)
            ++pending;
    }
    if (leases_.size() >= 4096)
        return {nullptr, Error::Limit};
    std::optional<std::size_t> selected;
    unsigned best = 0;
    for (std::size_t i = 0; i < rooms.size(); ++i)
    {
        if ((!server.empty() && rooms[i].id != server) ||
            rooms[i].server->match().phase() != MatchPhase::Lobby)
            continue;
        const auto slots = occupied(i);
        const auto used = unsigned(slots[0]) + unsigned(slots[1]);
        if (used >= 2)
            continue;
        if (!selected || used > best)
        {
            selected = i;
            best = used;
        }
    }
    if (!selected)
        return {nullptr, Error::Full};
    if (pending >= 4)
        return {nullptr, Error::Limit};
    const auto slots = occupied(*selected);
    const PlayerId player = slots[0] ? 2 : 1;
    Lease lease{network::randomToken(), request, ip, nickname, *selected, player, Clock::now() + duration_};
    const auto inserted = leases_.emplace(lease.token, lease);
    return {&inserted.first->second, Error::None, true};
}

void Reservations::cancel(const std::string& token)
{
    const auto found = leases_.find(token);
    if (found != leases_.end() && found->second.state == State::Pending)
        found->second.state = State::Cancelled;
}

std::optional<network::Admission> Reservations::admit(std::size_t room, const std::string& token,
                                                      const std::string& nickname)
{
    expire();
    const auto found = leases_.find(token);
    if (found == leases_.end())
        return {};
    auto& lease = found->second;
    const auto& match = pool_.rooms()[room].server->match();
    if (lease.state != State::Pending || lease.room != room || lease.nickname != nickname ||
        match.phase() != MatchPhase::Lobby || match.snapshot().connected[lease.player - 1])
        return {};
    lease.state = State::Consumed;
    return network::Admission{lease.player, lease.nickname};
}
} // namespace cubeshot::online
