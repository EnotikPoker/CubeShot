#include "online/Reservations.h"
#include "network/Identity.h"
#include "online/ArenaPool.h"
#include <algorithm>

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

std::array<bool, maxPlayers> Reservations::occupied(std::size_t room) const
{
    const auto state = pool_.rooms()[room].server->match().snapshot();
    std::array<bool, maxPlayers> slots{};
    for (unsigned i = 0; i < state.rules.participants; ++i)
        slots[i] = state.slots[i] == SlotKind::Human;
    for (const auto& entry : leases_)
    {
        const auto& lease = entry.second;
        if (lease.room == room && lease.state == State::Pending)
            slots[lease.player - 1] = true;
    }
    return slots;
}

Reservations::Result Reservations::reserve(const std::string& ip, const std::string& nickname,
                                           const std::string& request, const std::string& server,
                                           MatchRules preferences)
{
    expire();
    if (!preferences.valid())
        return {nullptr, Error::Conflict};
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
        const auto state = rooms[i].server->match().snapshot();
        const auto used = unsigned(std::count(slots.begin(), slots.end(), true));
        if (server.empty() && used &&
            (state.rules.format != preferences.format ||
             state.rules.participants != preferences.participants || state.rules.mode != preferences.mode))
            continue;
        if (used >= (used || !server.empty() ? state.rules.participants : preferences.participants))
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
    auto slots = occupied(*selected);
    if (server.empty() && std::none_of(slots.begin(), slots.end(), [](bool x) { return x; }))
        rooms[*selected].server->prepareRoom(preferences);
    PlayerId player = 0;
    for (unsigned i = 0; i < rooms[*selected].server->match().snapshot().rules.participants; ++i)
        if (!slots[i])
        {
            player = i + 1;
            break;
        }
    if (!player)
        return {nullptr, Error::Full};
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
        match.phase() != MatchPhase::Lobby ||
        (lease.player > match.snapshot().rules.participants ||
         match.snapshot().slots[lease.player - 1] == SlotKind::Human))
        return {};
    lease.state = State::Consumed;
    return network::Admission{lease.player, lease.nickname};
}
} // namespace cubeshot::online
