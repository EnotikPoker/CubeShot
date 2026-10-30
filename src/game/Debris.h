#pragma once
#include "game/Bullet.h"
#include "game/Player.h"
#include "game/Shape.h"
#include <optional>
#include <vector>

namespace cubeshot
{
struct PhysicsPiece
{
    std::uint64_t id = 0;
    PlayerId owner = 0;
    Vec2 position, velocity, size{12, 8};
    float angle = 0, spin = 0;
    Polygon outline, hull;
    Vec2 skinOffset{24, 24};
    int integrity = 20;
    unsigned revision = 0;
    bool charred = false;
    float sleep = 0;
};

struct Corpse
{
    PhysicsPiece body;
    int integrity = 100;
    WeaponKind cause = WeaponKind::Pistol;
};

struct DebrisHit
{
    float fraction = 2;
    bool fragment = false;
    std::size_t index = 0;
};

Vec2 piecePoint(const PhysicsPiece& piece, Vec2 local);
std::optional<float> hitPiece(Vec2 from, Vec2 to, const PhysicsPiece& piece, float radius = Bullet::radius);
void refreshPiece(PhysicsPiece& piece);

class Debris
{
public:
    static constexpr std::size_t maxPieces = 64;
    void clear();
    void blast(Vec2 at, float radius);
    void spawn(const Player& player, Vec2 velocity, Vec2 impact, WeaponKind cause = WeaponKind::Pistol,
               int volleyDamage = 0);
    void update(float dt, const Arena& arena, const std::vector<Player>& players);
    std::optional<DebrisHit> trace(Vec2 from, Vec2 to) const;
    bool hit(const DebrisHit& target, const Bullet& bullet, Vec2 position);
    const std::vector<Corpse>& corpses() const;
    const std::vector<PhysicsPiece>& pieces() const;

private:
    void split(PhysicsPiece source, unsigned count, Vec2 impulse);
    std::vector<Corpse> corpses_;
    std::vector<PhysicsPiece> pieces_;
    std::uint64_t nextId_ = 1;
};
} // namespace cubeshot
