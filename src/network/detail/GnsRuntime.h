#pragma once

#include "network/Protocol.h"
#include <chrono>
#include <functional>
#include <steam/steamnetworkingsockets.h>

namespace cubeshot::network::detail
{
using Clock = std::chrono::steady_clock;
using Handler = std::function<void(SteamNetConnectionStatusChangedCallback_t*)>;

// Shared by sessions on the one simulation/application thread.
class GnsRuntime
{
public:
    GnsRuntime();
    ~GnsRuntime();
    GnsRuntime(const GnsRuntime&) = delete;
    GnsRuntime& operator=(const GnsRuntime&) = delete;
};

void setConnectionHandler(HSteamNetConnection connection, Handler handler);
void removeConnectionHandler(HSteamNetConnection connection);
void setListenerHandler(HSteamListenSocket listener, Handler handler);
void removeListenerHandler(HSteamListenSocket listener);
void options(SteamNetworkingConfigValue_t (&out)[4]);
bool send(HSteamNetConnection connection, const wire::Envelope& message, bool reliable);
float age(Clock::time_point time);
} // namespace cubeshot::network::detail
