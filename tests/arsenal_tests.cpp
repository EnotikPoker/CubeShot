#include "game/World.h"
#include "network/Protocol.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace cubeshot;

namespace
{
constexpr float dt = 1.f / 120;

void expect(bool value, const char* text)
{
    if (!value)
        throw std::runtime_error(text);
}

Arena flat(float opponent = 350)
{
    return {{{0, 0}, {1400, 600}}, {{{0, 500}, {1400, 100}}}, {{100, 452}, {opponent, 452}}, MapId::Duel};
}

unsigned advance(World& w, int ticks)
{
    unsigned shots = 0;
    for (int n = 0; n < ticks; ++n)
    {
        w.update(dt);
        for (const auto& e : w.takeEvents())
            shots += e.kind == EventKind::Shot;
    }
    return shots;
}

void minigun()
{
    World w(flat(1200), 2);
    w.equip(1, WeaponKind::Minigun);
    w.setPlayerInput(1, {0, false, true, {1, 0}, true});
    expect(advance(w, 12) == 0, "Minigun cannot instantly fire at full speed");
    const auto ramp = advance(w, 108), full = advance(w, 120);
    expect(full > ramp && full >= 20, "Minigun gradually accelerates to a high fire rate");
    bool overheated = false;
    for (int n = 0; n < 600; ++n)
    {
        advance(w, 1);
        if (w.players()[0].weapon.overheated)
        {
            overheated = true;
            break;
        }
    }
    expect(overheated, "Sustained minigun fire overheats");
    expect(advance(w, 120) == 0, "Overheated minigun locks firing while cooling");
    w.clearPlayerInput(1);
    advance(w, 360);
    expect(!w.players()[0].weapon.overheated && w.players()[0].weapon.heat < .01f,
           "Minigun recovers without ammo/reload");
}

void melee()
{
    for (auto kind : {WeaponKind::Bat, WeaponKind::Katana, WeaponKind::Knife})
    {
        World w(flat(165), 2);
        w.equip(1, kind);
        w.setPlayerInput(1, {0, false, true, {1, 0}});
        advance(w, 1);
        expect(w.players()[1].health == 100 - weaponDefinition(kind).damage,
               "Melee deals its advertised damage once");
        expect(w.bullets().empty(), "Melee does not spawn long-range bullets");
        advance(w, 120);
        expect(w.players()[1].health == 100 - weaponDefinition(kind).damage,
               "Holding melee does not repeat swings");
        World far(flat(400), 2);
        far.equip(1, kind);
        far.setPlayerInput(1, {0, false, true, {1, 0}});
        advance(far, 1);
        expect(far.players()[1].health == 100, "Melee cannot hit beyond reach");
        World back(flat(165), 2);
        back.equip(1, kind);
        back.setPlayerInput(1, {0, false, true, {-1, 0}});
        advance(back, 1);
        expect(back.players()[1].health == 100, "Melee hits the aimed sector, never behind the attacker");
        auto a = flat(175);
        a.platforms.push_back({{158, 390}, {8, 110}});
        World wall(a, 2);
        wall.equip(1, kind);
        wall.setPlayerInput(1, {0, false, true, {1, 0}});
        advance(wall, 1);
        expect(wall.players()[1].health == 100, "Walls block melee attacks");
    }
}

void rockets()
{
    auto a = flat(410);
    a.spawnPoints.push_back({510, 452});
    a.platforms.push_back({{460, 300}, {12, 200}});
    World w(a, 3);
    w.equip(1, WeaponKind::Rpg);
    w.setPlayerInput(1, {0, false, true, {1, 0}});
    advance(w, 150);
    expect(w.players()[1].health < 100, "Rocket explodes on contact and damages nearby targets");
    World self(flat(1200), 2);
    self.equip(1, WeaponKind::Rpg);
    self.setPlayerInput(1, {0, false, true, {0, 1}});
    advance(self, 15);
    expect(self.players()[0].health < 100, "Rocket blast can hurt its shooter");
    // Equal impacts at a fixed wall: closer unshielded victim must take more damage.
    auto blastArena = [](float x)
    {
        auto a = flat(x);
        a.spawnPoints[0] = {350, 452};
        a.platforms.push_back({{300, 300}, {8, 200}});
        return a;
    };
    World near(blastArena(380), 2), far(blastArena(460), 2);
    for (auto* world : {&near, &far})
    {
        world->equip(1, WeaponKind::Rpg);
        world->setPlayerInput(1, {0, false, true, {-1, 0}});
        advance(*world, 25);
    }
    expect(near.players()[1].health < far.players()[1].health,
           "Explosion damage falls with distance from epicentre");
}

void flames()
{
    World w(flat(250), 2);
    w.equip(1, WeaponKind::Flamethrower);
    w.setPlayerInput(1, {0, false, true, {1, 0}, true});
    advance(w, 48);
    expect(w.players()[1].burning > 0, "Flamethrower ignites its target");
    w.clearPlayerInput(1);
    const auto hp = w.players()[1].health;
    advance(w, 60);
    expect(w.players()[1].health < hp, "Burning continues after trigger release");
    World self(flat(1200), 2);
    self.equip(1, WeaponKind::Flamethrower);
    self.setPlayerInput(1, {0, false, true, {0, 1}, true});
    advance(self, 40);
    expect(!self.fires().empty() && self.players()[0].health < 100, "Surface fire can ignite its owner");
    const auto radius = self.fires()[0].radius;
    self.clearPlayerInput(1);
    advance(self, 40);
    expect(self.fires()[0].radius > radius, "Surface fire grows over time");
    advance(self, 700);
    expect(self.fires().empty() && self.players()[0].burning == 0, "Fire expires and cannot burn forever");
    self.resetPlayers();
    expect(self.fires().empty() && self.players()[0].burning == 0, "Round reset removes fire and burning");
}

void boomerang()
{
    World w(flat(270), 2);
    w.equip(1, WeaponKind::FireBoomerang);
    w.setPlayerInput(1, {0, false, true, {1, 0}});
    unsigned hits = 0;
    bool returned = false;
    for (int i = 0; i < 250; ++i)
    {
        w.update(dt);
        for (const auto& b : w.bullets())
            returned = returned || b.returning;
        for (const auto& e : w.takeEvents())
            if (e.kind == EventKind::Hit && e.weapon == WeaponKind::FireBoomerang)
                ++hits;
    }
    expect(returned && hits == 2 && w.bullets().empty(),
           "Boomerang hits once outbound, once returning, and is caught");
    expect(!w.fires().empty() || w.players()[1].health < 60,
           "Fiery boomerang ignites targets and nearby surfaces");
}

void damageLocations()
{
    World w(flat(300), 2);
    w.setPlayerInput(1, {0, false, true, {1, -.07f}});
    bool localHit = false;
    for (int tick = 0; tick < 40; ++tick)
    {
        w.update(dt);
        for (const auto& event : w.takeEvents())
            if (event.kind == EventKind::Hit)
            {
                const auto local = w.players()[1].bodyPoint(event.local);
                expect(std::hypot(local.x + w.players()[1].position.x - event.position.x,
                                  local.y + w.players()[1].position.y - event.position.y) < .01f,
                       "Hit is attached in body-local coordinates, not an independent world-space sticker");
                expect(event.surfaceRevision > 0, "Hit carries the authoritative damage-surface revision");
                localHit = true;
            }
    }
    expect(localHit, "Shot generates a local impact event");
    const auto& shape = w.players()[1].surface;
    expect(!shape.marks.empty() && shape.marks[0].local.y < 40,
           "A high hit chips the upper body, not fixed lower corners");
    w.resetPlayers();
    expect(w.players()[1].surface.marks.empty(), "New round restores the body silhouette");
    World lethal(flat(300), 2);
    lethal.equip(1, WeaponKind::Sniper);
    lethal.setPlayerInput(1, {0, false, true, {1, 0}, true});
    bool death = false;
    for (int tick = 0; tick < 100; ++tick)
    {
        lethal.update(dt);
        for (const auto& event : lethal.takeEvents())
            if (event.kind == EventKind::Death)
            {
                death = true;
                expect(event.weapon == WeaponKind::Sniper && event.actor == 1 && event.volleyDamage == 100,
                       "Death carries the actual lethal weapon, shooter and complete volley damage");
            }
    }
    expect(death && lethal.debris().corpses()[0].cause == WeaponKind::Sniper &&
               std::count_if(lethal.debris().pieces().begin(), lethal.debris().pieces().end(),
                             [](const auto& p) { return p.kind == PieceKind::Corpse; }) == 3,
           "World chooses the sniper fracture profile from authoritative combat, not a default pistol");
}

void wireState()
{
    using namespace cubeshot::network;
    Match match;
    auto s = match.snapshot();
    s.players[0].weapon.equip(WeaponKind::Minigun);
    s.players[0].weapon.spin = .7f;
    s.players[0].weapon.heat = .9f;
    s.players[0].weapon.overheated = true;
    s.players[1].weapon.equip(WeaponKind::Flamethrower);
    s.players[1].burning = 2;
    s.players[1].burnOwner = 1;
    carveImpact(s.players[1].surface, {{0, 14}, {1, 0}, WeaponKind::Pistol, 20, 1, 1, 1});
    s.fires.push_back({8, 1, {200, 439}, 2, 25});
    Bullet b;
    b.id = 10;
    b.shooterId = 1;
    b.weapon = WeaponKind::FireBoomerang;
    b.returning = true;
    b.age = .6f;
    b.hitMask = 4;
    s.bullets.push_back(b);
    auto encoded = encodeSnapshot(s);
    auto out = decodeSnapshot(encoded);
    expect(out && out->fires.size() == 1 && out->players[0].weapon.overheated &&
               out->players[1].burning == 2 && out->players[1].surface.marks.size() == 1 &&
               out->bullets[0].returning,
           "Arsenal state survives network roundtrip");
    encoded.mutable_fires(0)->set_radius(std::numeric_limits<float>::infinity());
    expect(!decodeSnapshot(encoded), "Reject malformed fire state");
    for (unsigned kind = 0; kind < weaponCount; ++kind)
    {
        s.players[0].weapon.equip(static_cast<WeaponKind>(kind));
        expect(decodeSnapshot(encodeSnapshot(s)).has_value(), "Every weapon is valid on the wire");
    }
}
} // namespace

int main()
{
    try
    {
        minigun();
        melee();
        rockets();
        flames();
        boomerang();
        damageLocations();
        wireState();
        std::cout << "Arsenal mechanics passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
