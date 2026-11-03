#include "game/BotController.h"
#include "game/BotMobility.h"
#include "game/Collision.h"
#include "game/Prediction.h"
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

void expect(bool b, const char* message)
{
    if (!b)
        throw std::runtime_error(message);
}

Arena arena()
{
    return {{{0, 0}, {2000, 1000}}, {{{0, 900}, {2000, 100}}}, {{500, 852}, {1500, 852}}, MapId::Duel};
}

void magazines()
{
    for (unsigned k = 0; k < weaponCount; ++k)
    {
        Weapon w;
        w.equip(static_cast<WeaponKind>(k));
        const auto& d = weaponDefinition(w.kind);
        if (!d.capacity)
        {
            expect(!w.startReload(), "Melee and returning boomerang need no ammo");
            continue;
        }
        for (int n = 0; n < d.capacity; ++n)
        {
            w.cooldown = 0;
            w.spin = 1;
            w.heat = 0;
            w.overheated = false;
            expect(w.tryFire(), "Loaded rounds fire exactly once");
        }
        expect(w.ammo == 0 && w.reloading && !w.tryFire(),
               "Last round starts automatic reload; empty weapon cannot fire");
        w.update(w.kind == WeaponKind::Shotgun ? 4.f : d.reloadTime + .01f);
        expect(w.ammo == d.capacity && !w.reloading, "Infinite reserve refills a complete magazine");
        if (d.capacity == 1)
            continue;
        w.cooldown = 0;
        w.spin = 1;
        w.overheated = false;
        w.heat = 0;
        w.tryFire();
        expect(w.startReload(), "Manual reload accepts a partial magazine");
        float remaining = w.reloadRemaining;
        expect(!w.startReload() && w.reloadRemaining == remaining,
               "Repeated reload cannot restart its clock");
    }
    Weapon shotgun;
    shotgun.equip(WeaponKind::Shotgun);
    shotgun.ammo = 0;
    shotgun.startReload();
    shotgun.update(.84f);
    expect(shotgun.ammo == 0, "First shell needs preparation and insertion");
    shotgun.update(.02f);
    expect(shotgun.ammo == 1 && shotgun.reloading, "Shells become usable separately");
    expect(shotgun.tryFire() && shotgun.ammo == 0 && shotgun.reloading,
           "A ready shell interrupts insertion, fires, and empty gun reloads again");
}

void ballistics()
{
    Bullet pellet;
    pellet.weapon = WeaponKind::Shotgun;
    pellet.damage = 18;
    pellet.position = {0, -1000};
    pellet.velocity = {850, 0};
    pellet.acceleration = 240;
    pellet.lifetime = 8;
    pellet.update(.5f);
    expect(pellet.lifetime > 7 && pellet.travelled > 260 && pellet.impactDamage() < 18,
           "Spent pellet remains physical after the old .38-second cutoff");
    Bullet fine = pellet, coarse = pellet;
    coarse.update(.5f);
    for (int i = 0; i < 60; ++i)
        fine.update(dt);
    expect(std::hypot(fine.position.x - coarse.position.x, fine.position.y - coarse.position.y) < .05f,
           "Drag and gravity agree across different frame rates");
    expect(coarse.velocity.x < pellet.velocity.x && coarse.velocity.y > pellet.velocity.y &&
               coarse.impactDamage() >= 1,
           "Spent rounds slow down, fall, and retain a small impact");
}

void recoil()
{
    auto a = arena();
    World w(a, 2);
    w.equip(1, WeaponKind::Shotgun);
    w.requestAction(1, false, true, {0, 1});
    w.update(dt);
    expect(w.players()[0].velocity.y < -400 && w.players()[0].weapon.ammo == 5,
           "One shotgun blast uses one shell and one impulse, not six");
    for (int i = 0; i < 30; ++i)
    {
        w.setPlayerInput(1, {});
        w.update(dt);
    }
    expect(w.players()[0].position.y < 815, "A downwards shot lifts player from ground without jump");
    Player p;
    p.position = {500, 852};
    p.grounded = true;
    p.applyImpulse({400, 0});
    p.applyInput({1, false, false, {1, 0}});
    expect(p.velocity.x > Player::moveSpeed, "Movement preserves external horizontal velocity");
    p.clearInput();
    expect(p.velocity.x == p.pushX, "Clearing held input preserves physical impulse");
    World mini(a, 2);
    mini.equip(1, WeaponKind::Minigun);
    for (int i = 0; i < 360; ++i)
    {
        mini.setPlayerInput(1, {0, false, false, {0, 1}, true});
        mini.update(dt);
    }
    expect(mini.players()[0].position.y < 650, "Fully revved minigun can sustain flight");
}

void rockets()
{
    auto a = arena();
    a.spawnPoints[1] = {800, 852};
    World w(a, 2);
    w.equip(1, WeaponKind::Rpg);
    w.requestAction(1, false, true, {1, 0});
    int hits = 0;
    for (int i = 0; i < 90; ++i)
    {
        w.update(dt);
        for (auto e : w.takeEvents())
            if (e.kind == EventKind::Hit && e.target == 2)
                ++hits;
    }
    expect(!w.players()[1].alive() && hits == 1,
           "Direct rocket kills full HP with a single combined damage event");
    Player p;
    p.id = 1;
    p.position = {500, 852};
    auto e = blastEffect({680, 894}, p, a);
    expect(e.damage > 0 && e.damage <= 60 && e.impulse.y < 0,
           "A separated blast can boost safely while spending health");
    a.platforms.push_back({{600, 600}, {20, 300}});
    p.position = {650, 852};
    e = blastEffect({550, 870}, p, a);
    expect(e.damage == 0 && e.impulse.x == 0 && e.impulse.y == 0,
           "Solid cover blocks both blast damage and shove");
}

void replay()
{
    Match match(false, 42);
    match.connect(1);
    match.connect(2);
    match.setReady(1, true);
    match.setReady(2, true);
    for (int i = 0; i < 361; ++i)
        match.update(dt);
    auto s = match.snapshot();
    s.players[0].weapon.equip(WeaponKind::Shotgun);
    Prediction prediction;
    prediction.reconcile(s, 1);
    prediction.action(1, false, true, {0, 1});
    prediction.advance({0, false, false, {0, 1}});
    auto first = *prediction.player();
    prediction.reconcile(s, 1);
    expect(prediction.player()->weapon.ammo == 5 &&
               std::abs(prediction.player()->velocity.y - first.velocity.y) < .01f,
           "Reconcile replays ammo and kick once");
    prediction.action(2, false, false, {0, 1}, true);
    prediction.advance({});
    expect(prediction.player()->weapon.reloading, "Reliable reload is predicted");
    auto wire = network::encodeSnapshot(s);
    auto* p = wire.mutable_players(0);
    p->set_ammo(999);
    expect(!network::decodeSnapshot(wire), "Reject magazine count above capacity");
    wire = network::encodeSnapshot(s);
    p = wire.mutable_players(0);
    p->set_push_x(std::numeric_limits<float>::quiet_NaN());
    expect(!network::decodeSnapshot(wire), "Reject nonfinite impulse");
    wire = network::encodeSnapshot(s);
    expect(network::decodeSnapshot(wire).has_value(), "Valid new weapon states roundtrip");
}

void reloadAndRocketPrediction(MatchPhase phase = MatchPhase::Round)
{
    const auto a = Arena::createDuel();
    World world(a, 2);
    world.equip(1, WeaponKind::Rpg);
    Match match(false, 42);
    match.connect(1);
    match.connect(2);
    match.setReady(1, true);
    match.setReady(2, true);
    for (int i = 0; i < 361; ++i)
        match.update(dt);
    auto snapshot = match.snapshot();
    snapshot.phase = phase;
    snapshot.players = world.players();
    snapshot.bullets.clear();
    Prediction prediction;
    prediction.reconcile(snapshot, 1);
    PlayerInput input;
    input.aimDirection = {-.8660254f, .5f};
    world.requestAction(1, true, false, input.aimDirection, 1);
    prediction.action(1, true, false, input.aimDirection);
    bool hurt = false;
    for (int tick = 0; tick < 160; ++tick)
    {
        if (tick == 12)
        {
            world.requestAction(1, false, true, input.aimDirection, 2);
            prediction.action(2, false, true, input.aimDirection);
        }
        world.setPlayerInput(1, input);
        world.update(dt, phase == MatchPhase::Round);
        prediction.advance(input);
        const auto& actual = world.players()[0];
        const auto& predicted = *prediction.player();
        hurt = hurt || actual.health < 100;
        expect(actual.alive(), "Calculated rocket jump survives its own blast");
        expect(std::hypot(actual.position.x - predicted.position.x,
                          actual.position.y - predicted.position.y) < .1f,
               "Client predicts its own surviving rocket blast at the authority's contact and tick");
        expect(predicted.health == 100, "Prediction leaves HP to the server");
    }
    expect(hurt == (phase == MatchPhase::Round),
           "Aftermath immunity agrees with authoritative rocket physics");
    Player p;
    p.position = {300, 392};
    p.grounded = true;
    p.weapon.ammo = 6;
    p.weapon.startReload();
    p.applyInput({1, true, false, {0, -1}});
    p.update(.1f, a);
    expect(p.position.x > 300 && p.position.y < 392 && p.weapon.reloading && p.aimDirection.y == -1,
           "Reload preserves simultaneous movement, jump and aim");
}

void minigunSnapshotPrediction()
{
    World world(Arena::createDuel(), 2);
    world.equip(1, WeaponKind::Minigun);
    Match match(false, 42);
    match.connect(1);
    match.connect(2);
    match.setReady(1, true);
    match.setReady(2, true);
    for (int i = 0; i < 361; ++i)
        match.update(dt);
    auto snapshot = match.snapshot();
    Prediction prediction;
    PlayerInput held;
    held.aimDirection = {0, 1};
    held.triggerHeld = true;
    bool overheated = false, reloaded = false;
    for (int tick = 0; tick < 120 * 30; ++tick)
    {
        snapshot.players = world.players();
        snapshot.bullets = world.bullets();
        snapshot.inputAck[0] = prediction.sequence();
        snapshot.actionAck[0] = tick > 2800 ? 1 : 0;
        auto decoded = network::decodeSnapshot(network::encodeSnapshot(snapshot));
        expect(decoded.has_value(), "Live minigun state roundtrips through the wire");
        prediction.reconcile(*decoded, 1);
        held.triggerHeld = tick < 120 * 29;
        if (tick == 2800)
        {
            world.requestAction(1, false, false, held.aimDirection, 1, true);
            prediction.action(1, false, false, held.aimDirection, true);
        }
        world.setPlayerInput(1, held);
        world.update(dt);
        prediction.advance(held);
        const auto& authority = world.players()[0];
        overheated = overheated || authority.weapon.overheated;
        reloaded = reloaded || authority.weapon.reloading;
        const auto& local = *prediction.player();
        if (std::abs(authority.weapon.spin - local.weapon.spin) >= .0001f ||
            authority.weapon.ammo != local.weapon.ammo ||
            std::abs(authority.velocity.y - local.velocity.y) >= .001f)
            std::cerr << "Mini tick " << tick << " spin " << authority.weapon.spin << "/" << local.weapon.spin
                      << " ammo " << authority.weapon.ammo << "/" << local.weapon.ammo << " vy "
                      << authority.velocity.y << "/" << local.velocity.y << " cd "
                      << authority.weapon.cooldown << "/" << local.weapon.cooldown << " driven "
                      << authority.weapon.driven << "/" << local.weapon.driven << "\n";
        expect(std::abs(authority.weapon.spin - local.weapon.spin) < .0001f &&
                   authority.weapon.ammo == local.weapon.ammo &&
                   std::abs(authority.velocity.y - local.velocity.y) < .001f,
               "Frequent wire snapshots preserve minigun drive, ammo and flight impulse");
    }
    expect(overheated && reloaded,
           "Wire prediction covers overheat, cooling, belt replacement and trigger release");
}

void mobility()
{
    auto a = arena();
    a.platforms.push_back({{750, 740}, {220, 20}});
    a.spawnPoints[1] = {900, 692};
    for (bool hard : {false, true})
        for (auto kind : {WeaponKind::Shotgun, WeaponKind::Minigun, WeaponKind::Rpg})
        {
            Player p;
            p.id = 1;
            p.position = {500, 852};
            p.grounded = true;
            p.weapon.equip(kind);
            if (kind == WeaponKind::Minigun)
                p.weapon.heat = .94f;
            BotMobility planner(a, hard);
            bool moved = false, arrived = false;
            int startHp = p.health;
            std::vector<Bullet> rockets;
            for (int i = 0; i < 120 * 12; ++i)
            {
                PlayerInput in;
                in.aimDirection = p.aimDirection;
                bool controlled = planner.control(p, {800, 692}, rockets, {}, in, dt);
                expect(planner.lastSteps() <= 960, "Weapon navigation obeys fixed per-tick work budget");
                if (!controlled && !planner.executing() && moved)
                    break;
                p.applyInput(in);
                p.update(dt, a);
                rockets.erase(std::remove_if(rockets.begin(), rockets.end(),
                                             [&](Bullet& b)
                                             {
                                                 auto from = b.position;
                                                 b.update(dt);
                                                 float hit = 2;
                                                 for (auto wall : a.platforms)
                                                     if (auto t = collision::segmentHit(from, b.position,
                                                                                        wall, Bullet::radius))
                                                         hit = std::min(hit, *t);
                                                 if (hit <= 1)
                                                 {
                                                     Vec2 at{from.x + (b.position.x - from.x) * hit,
                                                             from.y + (b.position.y - from.y) * hit};
                                                     auto e = blastEffect(at, p, a);
                                                     p.applyImpulse(e.impulse);
                                                     p.health -= e.damage;
                                                     return true;
                                                 }
                                                 return false;
                                             }),
                              rockets.end());
                if (auto b = p.takeShot())
                {
                    moved = true;
                    if (kind == WeaponKind::Rpg)
                    {
                        float speed = std::hypot(b->velocity.x, b->velocity.y);
                        b->position.x += b->velocity.x / speed * p.weapon.barrel();
                        b->position.y += b->velocity.y / speed * p.weapon.barrel();
                        rockets.push_back(*b);
                    }
                }
                if (p.grounded && std::abs(p.position.y - 692) < 1)
                {
                    arrived = true;
                    break;
                }
            }
            if (!arrived)
                std::cerr << "Failed maneuver kind=" << static_cast<unsigned>(kind) << " pos=" << p.position.x
                          << "," << p.position.y << " shots=" << p.weapon.shots << " hp=" << p.health << "\n";
            expect(moved && arrived, "A planned weapon jump reaches a ledge beyond ordinary jump height");
            expect(p.health >= 40 && p.health <= startHp, "Rocket jump keeps at least forty HP");
            planner.reset();
            p.weapon.ammo = 0;
            PlayerInput blocked;
            expect(!planner.control(p, {800, 692}, {}, {}, blocked, dt), "Empty gun cannot start a maneuver");
            p.weapon.equip(kind);
            p.health = 39;
            expect(!planner.control(p, {800, 692}, {}, {}, blocked, dt),
                   "Low HP prevents risky weapon maneuvers");
            p.health = 100;
            p.weapon.overheated = true;
            expect(!planner.control(p, {800, 692}, {}, {}, blocked, dt),
                   "Overheated weapon cannot start a maneuver");
        }
}
} // namespace

int main()
{
    try
    {
        magazines();
        ballistics();
        recoil();
        rockets();
        replay();
        reloadAndRocketPrediction();
        reloadAndRocketPrediction(MatchPhase::RoundEnd);
        minigunSnapshotPrediction();
        mobility();
        std::cout << "Magazines, ballistic energy, recoil, blast, replay and weapon mobility passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
