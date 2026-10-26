#include "game/BotController.h"
#include "game/Prediction.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

namespace
{
constexpr float dt = 1.f / 120;

void expect(bool b, const char* why)
{
    if (!b)
        throw std::runtime_error(why);
}

Arena arena()
{
    return {{{0, 0}, {3000, 900}}, {{{0, 800}, {3000, 100}}}, {{100, 752}, {2700, 752}}, MapId::Duel};
}

unsigned advance(World& world, unsigned ticks)
{
    unsigned shots = 0;
    for (unsigned i = 0; i < ticks; ++i)
    {
        world.update(dt);
        for (const auto& event : world.takeEvents())
            shots += event.kind == EventKind::Shot;
    }
    return shots;
}

void weapons()
{
    for (unsigned i = 0; i < 7; ++i)
    {
        const auto kind = static_cast<WeaponKind>(i);
        auto definition = weaponDefinition(kind);
        expect(definition.damage > 0 && definition.interval > 0 && definition.speed > 0,
               "Weapon has valid parameters");
        World world(arena(), 2);
        world.equip(1, kind);
        world.setPlayerInput(1, {0, false, true, {1, 0}, true});
        const auto shots = advance(world, 120);
        if (definition.automatic)
            expect(shots >= 8 && shots <= static_cast<unsigned>(std::ceil(1 / definition.interval)) + 1,
                   "Held automatic fire follows the physical weapon interval");
        else
            expect(shots == 1, "Holding a semi-automatic trigger never produces repeated shots");
        world.setPlayerInput(1, {});
        expect(advance(world, 300) == 0, "Releasing the trigger stops automatic fire");
        world.setPlayerInput(1, {0, false, true, {1, 0}, true});
        expect(advance(world, 60) == 1 || definition.automatic, "New semi-automatic press can fire again");
    }
    World shotgun(arena(), 2);
    shotgun.equip(1, WeaponKind::Shotgun);
    shotgun.setPlayerInput(1, {0, false, true, {1, 0}});
    shotgun.update(dt);
    expect(shotgun.bullets().size() == 6, "Shotgun creates six separate pellets");
    for (const auto& b : shotgun.bullets())
        expect(b.damage == 18 && b.lifetime < .4f && b.volley == 1,
               "Shotgun damage, range and volley identity");
    expect(advance(shotgun, 60) == 1, "Shotgun produces one shot event per blast");
    expect(shotgun.bullets().empty(), "Pellets expire at short range");
    World sniper(arena(), 2);
    sniper.equip(1, WeaponKind::Sniper);
    sniper.setPlayerInput(1, {0, false, true, {1, 0}, true});
    expect(advance(sniper, 20) == 0 && sniper.players()[0].weapon.charging,
           "Sniper exposes preparation state without an early shot");
    sniper.setPlayerInput(1, {});
    expect(advance(sniper, 40) == 0 && !sniper.players()[0].weapon.charging,
           "Early release cancels preparation");
    sniper.setPlayerInput(1, {0, false, true, {1, 0}, true});
    expect(advance(sniper, 40) == 1 && sniper.bullets()[0].damage == 100,
           "Holding for preparation releases one lethal sniper bullet");
    expect(advance(sniper, 300) == 0, "Sniper still needs another press after cooldown");
    sniper.setPlayerInput(1, {});
    sniper.setPlayerInput(1, {0, false, true, {1, 0}, true});
    advance(sniper, 10);
    sniper.clearPlayerInput(1);
    expect(advance(sniper, 60) == 0, "Pause/focus reset cancels a prepared shot");
}

void armedBots()
{
    auto a = Arena::createDuel();
    for (unsigned kind = 0; kind < 7; ++kind)
        for (auto difficulty : {BotDifficulty::Easy, BotDifficulty::Medium, BotDifficulty::Hard})
        {
            World world(a, 2);
            world.equip(2, static_cast<WeaponKind>(kind));
            BotController bot(a, difficulty, 42);
            bool held = false, fired = false;
            for (int tick = 0; tick < 120 * 60 && world.players()[0].alive(); ++tick)
            {
                const auto input = bot.update(observeBot(world, 2, 1), dt);
                world.setPlayerInput(
                    2, {input.moveDirection, false, false, input.aimDirection, input.triggerHeld});
                if (input.jump || (input.shoot && !held))
                    world.requestAction(2, input.jump, input.shoot && !held, input.aimDirection);
                held = input.shoot;
                world.update(dt);
                for (const auto& event : world.takeEvents())
                    fired = fired || event.kind == EventKind::Shot;
            }
            if (!fired || world.players()[0].alive())
            {
                std::cerr << "Weapon=" << kind << " difficulty=" << static_cast<int>(difficulty)
                          << " target HP=" << world.players()[0].health << '\n';
                throw std::runtime_error("Bots must use each weapon, including automatic and prepared fire");
            }
        }
}

void prediction()
{
    Match match;
    match.connect(1);
    match.connect(2);
    match.setReady(1, true);
    match.setReady(2, true);
    match.update(3);
    auto snapshot = match.snapshot();
    for (auto kind : {WeaponKind::Shotgun, WeaponKind::Automatic, WeaponKind::Smg, WeaponKind::Sniper})
    {
        snapshot.players[0].weapon.equip(kind);
        Prediction prediction;
        prediction.reconcile(snapshot, 1);
        auto initial = prediction.action(1, false, true, {1, 0});
        unsigned shots = initial ? 1 : 0;
        for (int tick = 0; tick < 120; ++tick)
        {
            prediction.advance({0, false, false, {1, 0}, true});
            shots += static_cast<unsigned>(prediction.takeEvents().size());
        }
        if (kind == WeaponKind::Shotgun || kind == WeaponKind::Sniper)
            expect(shots == 1, "Predicted semi-auto blast is emitted exactly once");
        else
            expect(shots >= 8, "Held automatic shots are predicted between authoritative replies");
        const auto after = shots;
        for (int tick = 0; tick < 120; ++tick)
        {
            prediction.advance({});
            shots += static_cast<unsigned>(prediction.takeEvents().size());
        }
        expect(shots == after, "Predicted automatic fire also stops after release");
    }
}
} // namespace

int main()
{
    try
    {
        weapons();
        prediction();
        armedBots();
        std::cout << "Weapons and fire prediction passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
