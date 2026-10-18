#pragma once

#include "game/Match.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace cubeshot::network
{
// Все методы вызываются из одного потока. Библиотека сама обслуживает UDP.
struct Admission
{
    PlayerId player = 0;
    std::string nickname;
};

using AdmitPlayer = std::function<std::optional<Admission>(const std::string&, const std::string&)>;

class Server
{
public:
    Server(std::uint16_t port, std::string ownerToken = {}, AdmitPlayer admission = {});
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

} // namespace cubeshot::network
