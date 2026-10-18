#include "network/NetworkSimulation.h"
#include <steam/isteamnetworkingutils.h>

namespace cubeshot::network
{
void simulateNetwork(float loss, int lag, int jitter)
{
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Avg,
                                                      jitter / 2.f);
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Max,
                                                      static_cast<float>(jitter));
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Pct,
                                                      jitter ? 100.f : 0.f);
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, loss);
    SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send, lag);
}
} // namespace cubeshot::network
