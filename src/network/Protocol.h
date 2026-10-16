#pragma once
#include "game/Match.h"
#include "protocol.pb.h"
#include <optional>
#include <string>
namespace cubeshot::network {
constexpr unsigned protocolVersion = 3;
constexpr std::uint16_t gamePort = 27015, discoveryPort = 27016;
constexpr int maxPacketBytes = 64 * 1024;
wire::Envelope envelope();
bool decode(const void* data, int size, wire::Envelope& result);
wire::Snapshot encodeSnapshot(const WorldSnapshot& snapshot);
std::optional<WorldSnapshot> decodeSnapshot(const wire::Snapshot& snapshot);
bool validAim(const wire::Vec& aim);
}
