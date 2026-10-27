#pragma once
#include "game/Bullet.h"
#include "game/Player.h"
#include <optional>
#include <vector>

namespace cubeshot
{
struct PhysicsPiece
{
    std::uint64_t id = 0;
    PlayerId owner = 0;
    Vec2 position, velocity, size{12, 8}; // Position is the centre.
    float angle = 0, spin = 0;
};

struct Corpse
{
    PhysicsPiece body;
    int integrity = 100;
    std::uint32_t cells = 0xffff;
};

struct DebrisHit
{
    float fraction = 2;
    bool fragment = false;
    std::size_t index = 0;
    unsigned cell = 0;
};

Rect corpseCell(unsigned cell);
Vec2 piecePoint(const PhysicsPiece& piece, Vec2 local);
std::optional<float> hitPiece(Vec2 from, Vec2 to, const PhysicsPiece& piece, Rect local,
                              float radius = Bullet::radius);

class Debris
{
public:
    static constexpr std::size_t maxPieces = 128;
    void clear();
    void blast(Vec2 at, float radius);
    void spawn(const Player& player, Vec2 velocity, Vec2 impact);
    void update(float dt, const Arena& arena, const std::vector<Player>& players);
    std::optional<DebrisHit> trace(Vec2 from, Vec2 to) const;
    bool hit(const DebrisHit& target, const Bullet& bullet, Vec2 position);
    const std::vector<Corpse>& corpses() const;
    const std::vector<PhysicsPiece>& pieces() const;

private:
    void release(Corpse& corpse, unsigned cell, Vec2 impulse);
    std::uint64_t nextId_ = 1;
    std::vector<Corpse> corpses_;
    std::vector<PhysicsPiece> pieces_;
};
} // namespace cubeshot
