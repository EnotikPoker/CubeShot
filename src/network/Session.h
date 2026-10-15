#pragma once
#include "network/Protocol.h"
#include <memory>
namespace cubeshot::network {
// Все методы вызываются из одного потока. Библиотека сама обслуживает UDP.
class Server {
public:
    Server(std::uint16_t port, std::string ownerToken = {});
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    void poll();
    void step(float dt = 1.0f / 120);
    const Match& match() const;
    std::uint16_t port() const;
    bool hostLeft() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class Client {
public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    bool connect(const std::string& address, const std::string& ownerToken = {});
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
// Только для воспроизводимых тестов транспорта; 0 отключает имитацию.
void simulateNetwork(float lossPercent, int lagMilliseconds, int jitterMilliseconds = 0);
bool validAddress(const std::string& address);
}
