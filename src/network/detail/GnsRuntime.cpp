#include "network/detail/GnsRuntime.h"
#include <map>
#include <stdexcept>
#include <utility>

namespace cubeshot::network::detail
{
namespace
{
std::map<HSteamNetConnection, Handler> connections;
std::map<HSteamListenSocket, Handler> listeners;
unsigned users = 0;

void changed(SteamNetConnectionStatusChangedCallback_t* event)
{
    auto found = connections.find(event->m_hConn);
    if (found != connections.end())
    {
        auto handler = found->second;
        handler(event);
        return;
    }
    auto listener = listeners.find(event->m_info.m_hListenSocket);
    if (listener != listeners.end())
    {
        auto handler = listener->second;
        handler(event);
    }
}
} // namespace

GnsRuntime::GnsRuntime()
{
    if (users == 0)
    {
        SteamDatagramErrMsg error;
        if (!GameNetworkingSockets_Init(nullptr, error))
            throw std::runtime_error(error);
    }
    ++users;
}

GnsRuntime::~GnsRuntime()
{
    if (--users == 0)
        GameNetworkingSockets_Kill();
}

void setConnectionHandler(HSteamNetConnection connection, Handler handler)
{
    connections[connection] = std::move(handler);
}

void removeConnectionHandler(HSteamNetConnection connection)
{
    connections.erase(connection);
}

void setListenerHandler(HSteamListenSocket listener, Handler handler)
{
    listeners[listener] = std::move(handler);
}

void removeListenerHandler(HSteamListenSocket listener)
{
    listeners.erase(listener);
}

void options(SteamNetworkingConfigValue_t (&out)[4])
{
    out[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                  reinterpret_cast<void*>(changed));
    out[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
    out[2].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, 5000);
    out[3].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, 5000);
}

bool send(HSteamNetConnection connection, const wire::Envelope& message, bool reliable)
{
    const std::string bytes = message.SerializeAsString();
    return SteamNetworkingSockets()->SendMessageToConnection(
               connection, bytes.data(), static_cast<uint32>(bytes.size()),
               reliable ? k_nSteamNetworkingSend_ReliableNoNagle : k_nSteamNetworkingSend_UnreliableNoNagle,
               nullptr) == k_EResultOK;
}

float age(Clock::time_point time)
{
    return std::chrono::duration<float>(Clock::now() - time).count();
}
} // namespace cubeshot::network::detail
