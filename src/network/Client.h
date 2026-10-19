#pragma once

#include "game/Match.h"
#include <memory>
#include <optional>
#include <string>

namespace cubeshot::network
{
// All methods are called by the application thread.
class Client
{
public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    bool connect(const std::string& address, const std::string& ownerToken = {},
                 const std::string& nickname = "Игрок", const std::string& reservationToken = {});
    void disconnect();
    void poll();
    void control(PlayerInput input);
    void action(bool jump, bool shoot, Vec2 aim);
    void ready(bool value);
    bool connected() const;
    bool connecting() const;
    PlayerId playerId() const;
    const std::string& error() const;
    const std::optional<WorldSnapshot>& snapshot() const;
    WorldSnapshot interpolated() const;
    const std::optional<Player>& predictedPlayer() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cubeshot::network
