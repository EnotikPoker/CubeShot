#include "game/Arena.h"
#include "game/Debris.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

namespace
{
void expect(bool b, const char* why)
{
    if (!b)
        throw std::runtime_error(why);
}

Arena arena()
{
    return {{{0, 0}, {960, 540}}, {{{0, 440}, {960, 100}}}, {{100, 392}, {300, 392}}, MapId::Duel};
}

void physical()
{
    auto a = arena();
    Player dead;
    dead.id = 2;
    dead.position = {300, 392};
    dead.health = 0;
    Debris debris;
    debris.spawn(dead, {}, {1, 0});
    for (int tick = 0; tick < 240; ++tick)
        debris.update(1.f / 120, a, {});
    expect(debris.corpses().size() == 1 && debris.corpses()[0].body.outline.size() == 4,
           "Death creates one physical body");
    const auto settled = debris.corpses()[0].body;
    expect(settled.position.y > 410 && settled.position.y < 440, "Corpse falls and rests on terrain");
    Player living;
    living.id = 1;
    living.position = {settled.position.x - 24, 392};
    living.velocity.x = 280;
    for (int tick = 0; tick < 60; ++tick)
        debris.update(1.f / 120, a, {living});
    expect(debris.corpses()[0].body.position.x > settled.position.x + 15,
           "Walking pushes corpse without blocking living player");
    expect(living.position.x == settled.position.x - 24,
           "Debris never changes the walking player's position");
    Bullet bullet;
    bullet.damage = 20;
    bullet.velocity = {800, 0};
    for (unsigned count = 0; count < 5; ++count)
    {
        const auto& corpse = debris.corpses()[0];
        expect(!corpse.body.outline.empty(), "Body retains material before final hit");
        const auto local = corpse.body.outline.front();
        const auto centre = piecePoint(corpse.body, local);
        auto hit = debris.trace({centre.x - 100, centre.y}, centre);
        if (count == 0)
            expect(hit && !hit->fragment, "Ray hits the physical body");
        const bool broken = debris.hit(DebrisHit{.5f, false, 0}, bullet, centre);
        expect(broken == (count == 4), "Body breaks progressively after accumulated damage");
        expect(debris.corpses()[0].integrity == 100 - static_cast<int>(count + 1) * 20,
               "Corpse integrity follows real bullet damage");
    }
    expect(debris.corpses()[0].body.outline.empty() && debris.pieces().size() >= 4,
           "Final hit releases irregular physical fragments");
    for (int tick = 0; tick < 600; ++tick)
        debris.update(1.f / 120, a, {});
    for (const auto& piece : debris.pieces())
        expect(std::isfinite(piece.position.x) && std::isfinite(piece.angle) && piece.position.y <= 440 &&
                   piece.position.x >= 0 && piece.position.x <= 960,
               "Fragments remain finite and collide with floor and arena bounds");
    debris.clear();
    expect(debris.corpses().empty() && debris.pieces().empty(), "Round reset removes all remains");
}
} // namespace

int main()
{
    try
    {
        physical();
        std::cout << "Physical corpses and fragmentation passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
