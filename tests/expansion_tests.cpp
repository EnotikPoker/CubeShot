#include "game/AttackStats.h"
#include "game/BotController.h"
#include "game/Collision.h"
#include "game/World.h"
#include "network/Protocol.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;
constexpr float dt = 1.f / 120;

void expect(bool v, const char* why)
{
    if (!v)
        throw std::runtime_error(why);
}

Arena flat(float enemy = 300)
{
    return {{{0, 0}, {1200, 540}}, {{{0, 440}, {1200, 100}}}, {{100, 392}, {enemy, 392}}, MapId::Duel};
}

void tick(World& w, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        w.update(dt);
}

void catalogs()
{
    expect(weaponCount == 21 && abilityCount == 47 && activeCount == 14 && playableMaps().size() == 21,
           "Complete expansion catalogs");
    for (auto id : playableMaps())
    {
        auto a = Arena::create(id);
        expect(a.controlAnchors().size() == 3 && a.respawnPoints().size() >= 4,
               "All maps support control and respawns");
        for (auto at : a.spawnPoints)
            for (auto wall : a.platforms)
                expect(!collision::overlaps({at, {48, 48}}, wall), "Spawn never intersects a solid");
    }
    Abilities a;
    auto pool = a.available(WeaponKind::Beam);
    expect(std::find(pool.begin(), pool.end(), AbilityKind::TwinShot) == pool.end() &&
               std::find(pool.begin(), pool.end(), AbilityKind::Economy) != pool.end(),
           "Beam receives only compatible perks");
}

void weapons()
{
    World beam(flat(), 2);
    beam.equip(1, WeaponKind::Beam);
    beam.setPlayerInput(1, {0, false, false, {1, 0}, true});
    tick(beam, 120);
    expect(beam.players()[1].health < 40, "Beam deals continuous damage and ramps up");
    World hammer(flat(220), 2);
    hammer.equip(1, WeaponKind::Hammer);
    hammer.requestAction(1, false, true, {1, 0});
    tick(hammer, 1);
    expect(hammer.players()[1].health == 55 && hammer.players()[1].pushX > 0,
           "Hammer deals 45 and pushes on melee hit");
    World sticky(flat(), 2);
    sticky.equip(1, WeaponKind::StickyLauncher);
    sticky.requestAction(1, false, true, {1, 0});
    for (int i = 0; i < 120 && sticky.entities().empty(); ++i)
        sticky.update(dt);
    expect(!sticky.entities().empty() && sticky.entities()[0].kind == EntityKind::Sticky,
           "Sticky projectile creates shootable attachment");
    tick(sticky, 160);
    expect(sticky.entities().empty() && sticky.players()[1].health < 95,
           "Attached charge eventually explodes");
}

void skills()
{
    World w(flat(800), 2);
    w.setActives(1, {ActiveKind::Mine, ActiveKind::Screen});
    w.requestAction(1, false, false, {1, 0}, 1, false, 1);
    tick(w, 1);
    expect(w.entities().size() == 1 && w.entities()[0].kind == EntityKind::Mine, "Mine installs on ground");
    w.requestAction(1, false, false, {1, 0}, 2, false, 2);
    tick(w, 1);
    expect(w.entities().size() == 2 && w.arena().combatScreens.size() == 1,
           "Screen installs and blocks attacks without adding body terrain");
    tick(w, 365);
    expect(w.arena().combatScreens.empty(), "Screen expires and clears collision query");
    World reflect(flat(350), 2);
    reflect.setActives(2, {ActiveKind::Parry, ActiveKind::None});
    reflect.requestAction(1, false, true, {1, 0});
    tick(reflect, 28);
    reflect.requestAction(2, false, false, {-1, 0}, 1, false, 1);
    tick(reflect, 75);
    expect(reflect.players()[1].health == 100 && reflect.players()[0].health < 100,
           "Timed parry returns bullet to attacker");
    Player p;
    p.burning = 2;
    p.bleeding = 3;
    p.slow = 2;
    p.setActives({ActiveKind::Cleanse, ActiveKind::Overdrive});
    expect(p.activate(0, flat()) && p.cleanse > 0 && p.burning == 0 && p.slow == 0,
           "Cleanse clears damage and control states");
    Abilities a;
    a.add(AbilityKind::Transfer);
    a.add(AbilityKind::Transfer);
    a.add(AbilityKind::Resonance);
    p.setAbilities(a);
    p.actives[0].cooldown = 10;
    expect(p.activate(1, flat()) && p.actives[0].cooldown == 9 && p.resonance == 2,
           "Cast applies transfer and resonance exactly once");
}

void coverAndRicochet()
{
    auto arena = flat(900);
    arena.platforms.push_back({{260, 250}, {20, 190}});
    World disc(arena, 2);
    disc.equip(1, WeaponKind::Disc);
    disc.requestAction(1, false, true, {1, 0});
    tick(disc, 60);
    expect(disc.players()[0].health < 100 && disc.players()[1].health == 100,
           "A disk reflected by a wall can damage its owner while the opponent remains covered");

    World screen(flat(500), 2);
    screen.setActives(1, {ActiveKind::Screen, ActiveKind::None});
    screen.requestAction(1, false, false, {1, 0}, 1, false, 1);
    tick(screen, 1);
    screen.requestAction(2, false, true, {-1, 0});
    tick(screen, 60);
    expect(screen.players()[0].health == 100 && screen.entities().size() == 1 &&
               screen.entities()[0].health == 70,
           "A screen intercepts bullets with its own HP before the body");
    auto& target = const_cast<Player&>(screen.players()[0]);
    auto& attacker = const_cast<Player&>(screen.players()[1]);
    target.position = {180, 392};
    attacker.position = {290, 392};
    screen.equip(2, WeaponKind::Hammer);
    screen.requestAction(2, false, true, {-1, 0});
    tick(screen, 1);
    expect(target.health == 100 && screen.entities()[0].health == 25,
           "A screen blocks melee contact and takes hammer damage");
    tick(screen, 104);
    screen.requestAction(2, false, true, {-1, 0});
    tick(screen, 1);
    expect(screen.entities().empty() && screen.arena().combatScreens.empty() && target.health == 100,
           "Destroying a screen clears every collision query without hitting through the same attack");
}

void wireRoundTrip()
{
    Match match;
    auto s = match.snapshot();
    World w(Arena::create(MapId::LabSectors), 2);
    s.map = MapId::LabSectors;
    s.terrain = w.arena().terrainState();
    s.players = w.players();
    s.entities = w.entities();
    auto encoded = network::encodeSnapshot(s);
    expect(network::decodeSnapshot(encoded).has_value(), "Entity and new map wire roundtrip");
    encoded.mutable_entities(0)->set_health(999);
    expect(!network::decodeSnapshot(encoded), "Reject invalid entity health");
}

int main()
{
    try
    {
        catalogs();
        weapons();
        skills();
        coverAndRicochet();
        wireRoundTrip();
        std::cout << "Expansion combat, entities and wire passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
