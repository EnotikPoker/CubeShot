#pragma once
#include "network/Protocol.h"

namespace cubeshot::network
{
void encodeExpansion(wire::Snapshot&, const WorldSnapshot&);
bool decodeExpansion(const wire::Snapshot&, WorldSnapshot&);
void encodePlayerExpansion(wire::Player&, const Player&);
bool decodePlayerExpansion(const wire::Player&, Player&);
void encodeBulletExpansion(wire::Bullet&, const Bullet&);
bool decodeBulletExpansion(const wire::Bullet&, Bullet&);
} // namespace cubeshot::network
