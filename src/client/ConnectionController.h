#pragma once

#include "client/HostProcess.h"
#include "client/Menu.h"
#include "network/Client.h"
#include "network/Directory.h"
#include "network/Discovery.h"
#include <cstdint>
#include <memory>
#include <string>

namespace cubeshot::client
{
// Owns connection lifetime and the LAN/online menu flow. Input and rendering stay in Application.
class ConnectionController
{
public:
    ~ConnectionController();
    void setDirectoryUrl(std::string url);
    void handle(MenuAction action, Menu& menu, std::string& nickname);
    void update(Menu& menu, const std::string& nickname);
    void stop();
    network::Client* session();
    const network::Client* session() const;

private:
    void updateDirectory(Menu& menu, const std::string& nickname);
    std::unique_ptr<network::Client> client_;
    std::unique_ptr<network::Discovery> discovery_;
    HostProcess host_;
    network::Directory directory_;
    std::string directoryUrl_, reservationToken_;
    bool hosting_ = false;
    bool onlineSession_ = false;
    std::uint64_t nextDirectoryRefresh_ = 0;
};
} // namespace cubeshot::client
