#pragma once

#include "network/Server.h"
#include <array>
#include <chrono>
#include <map>
#include <string>

namespace cubeshot::online
{
class ArenaPool;

// Domain state only: no HTTP, JSON or worker threads. Used by the simulation thread.
class Reservations
{
public:
    using Clock = std::chrono::steady_clock;
    enum class State
    {
        Pending,
        Consumed,
        Cancelled
    };

    struct Lease
    {
        std::string token, request, ip, nickname;
        std::size_t room;
        PlayerId player;
        Clock::time_point expires;
        State state = State::Pending;
    };
    enum class Error
    {
        None,
        Conflict,
        Cancelled,
        Full,
        Limit
    };

    struct Result
    {
        const Lease* lease = nullptr;
        Error error = Error::None;
        bool created = false;
    };

    Reservations(const ArenaPool& pool, std::chrono::milliseconds duration);
    Result reserve(const std::string& ip, const std::string& nickname, const std::string& request,
                   const std::string& server);
    void cancel(const std::string& token);
    std::optional<network::Admission> admit(std::size_t room, const std::string& token,
                                            const std::string& nickname);
    std::array<bool, 2> occupied(std::size_t room) const;
    void expire();

private:
    const ArenaPool& pool_;
    std::chrono::milliseconds duration_;
    std::map<std::string, Lease> leases_;
};
} // namespace cubeshot::online
