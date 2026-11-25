#include "game/AttackQueries.h"
#include "game/BotController.h"
#include "game/BotQueries.h"
#include <iostream>
#include <stdexcept>
using namespace cubeshot;
constexpr float dt = 1.f / 120;

void expect(bool b, const char* message)
{
    if (!b)
        throw std::runtime_error(message);
}

void headEncounters()
{
    unsigned cases = 0;
    for (auto difficulty : {BotDifficulty::Easy, BotDifficulty::Medium, BotDifficulty::Hard})
        for (auto weapon : {WeaponKind::Bat, WeaponKind::Katana, WeaponKind::Knife})
            for (bool left : {false, true})
                for (bool smallBot : {false, true})
                    for (bool smallHuman : {false, true})
                    {
                        auto arena = Arena::create(MapId::FactoryPartitions);
                        float x = left ? 218.f : 690.f;
                        arena.spawnPoints = {{x, 264}, {x, 312}};
                        World world(arena, 2);
                        Abilities human, botTraits;
                        if (smallHuman)
                            human.add(AbilityKind::SmallBody);
                        if (smallBot)
                            botTraits.add(AbilityKind::SmallBody);
                        world.setAbilities(1, human);
                        world.setAbilities(2, botTraits);
                        world.equip(2, weapon);
                        world.setPlayerInput(2, {0, false, false, {0, 1}});
                        world.update(dt);
                        BotController bot(arena, difficulty, 42);
                        bool held = false, upward = false;
                        for (int tick = 0; tick < 720 && world.players()[0].health == 100; ++tick)
                        {
                            auto input = bot.update(observeBot(world, 2, 1), dt);
                            upward = upward || input.aimDirection.y < -.2f;
                            world.setPlayerInput(2, {input.moveDirection, false, false, input.aimDirection,
                                                     input.triggerHeld, false, input.jetpackHeld});
                            if (input.reload)
                                world.requestAction(2, false, false, input.aimDirection, 0, true);
                            if (input.jump || (input.shoot && !held))
                                world.requestAction(2, input.jump, input.shoot && !held, input.aimDirection);
                            held = input.shoot;
                            world.update(dt);
                            world.takeEvents();
                        }
                        if (!upward || world.players()[0].health == 100)
                            std::cerr << "head difficulty=" << unsigned(difficulty)
                                      << " weapon=" << unsigned(weapon) << " left=" << left
                                      << " sizes=" << smallBot << smallHuman << "\n";
                        expect(upward && world.players()[0].health < 100,
                               "A bot attacks a stationary rider within six seconds in every "
                               "size/difficulty/melee combination");
                        ++cases;
                    }
    std::cout << "Head encounter cases: " << cases << '\n';
}

void supportPrediction()
{
    auto arena = Arena::createDuel();
    arena.spawnPoints = {{100, 344}, {100, 392}};
    World world(arena, 2);
    world.update(dt);
    auto view = observeBot(world, 2, 1);
    auto future = botdetail::predictBotTarget(*view.target, view.self, .5f, arena);
    expect(std::abs(future.position.y - world.players()[0].position.y) < .1f,
           "Opponent prediction preserves support on a player instead of falling through its head");
}

void realPenetration()
{
    Arena arena{{{0, 0}, {960, 540}}, {{{0, 440}, {960, 100}}}, {{100, 392}, {650, 392}}, MapId::Duel};
    arena.addTerrain({{400, 360}, {8, 80}}, Material::Wood);
    arena.rebuildTerrain();
    World world(arena, 2);
    world.equip(1, WeaponKind::Crossbow);
    auto view = observeBot(world, 1, 2);
    auto aim = botAim(view.self, *view.target, arena);
    expect(aim.has_value(), "Crossbow bot recognises its innate wood penetration");
    world.requestAction(1, false, true, aim->direction);
    for (int i = 0; i < 120; ++i)
        world.update(dt);
    expect(world.players()[1].health < 100, "Planned penetration produces actual HP damage");
    auto at = world.players()[1].weaponOrigin();
    Player defender = world.players()[1];
    Abilities skin;
    skin.add(AbilityKind::ToughSkin);
    skin.add(AbilityKind::FireResistance);
    defender.setAbilities(skin);
    expect(healthDamage(20, 1, defender, WeaponKind::Flamethrower) == 9,
           "Bot and authoritative damage share resistance calculations");
}

void explosivesBothSlots()
{
    for (auto kind : {WeaponKind::Rpg, WeaponKind::GrenadeLauncher})
        for (PlayerId shooter : {1u, 2u})
        {
            Arena arena{
                {{0, 0}, {1400, 600}}, {{{0, 500}, {1400, 100}}}, {{100, 452}, {700, 452}}, MapId::Duel};
            World world(arena, 2);
            world.equip(shooter, kind);
            auto view = observeBot(world, shooter, 3 - shooter);
            auto aim = botAim(view.self, *view.target, arena);
            expect(aim.has_value(),
                   "Explosive attack plans work for either player ID despite sorted body prediction");
            world.requestAction(shooter, false, true, aim->direction);
            for (int i = 0; i < 240; ++i)
                world.update(dt);
            expect(world.players()[2 - shooter].health < 100,
                   "Planned explosive shot deals real enemy damage");
            expect(world.players()[shooter - 1].health == 100, "Safe explosive plan avoids self damage");
        }
}

void bodySizedOpening()
{
    Arena arena{{{0, 0}, {960, 540}}, {{{0, 440}, {960, 100}}}, {{36, 392}, {536, 392}}, MapId::Duel};
    arena.addTerrain({{512, 290}, {24, 112}}, Material::Wood);
    arena.rebuildTerrain();
    World world(arena, 2);
    world.equip(2, WeaponKind::Flamethrower);
    BotController bot(arena, BotDifficulty::Hard, 42);
    bool held = false;
    for (int i = 0; i < 2400 && world.players()[0].health == 100 && world.players()[1].alive(); ++i)
    {
        auto input = bot.update(observeBot(world, 2, 1), dt);
        world.setPlayerInput(2, {input.moveDirection, false, false, input.aimDirection, input.triggerHeld,
                                 false, input.jetpackHeld});
        if (input.jump || (input.shoot && !held) || input.reload)
            world.requestAction(2, input.jump, input.shoot && !held, input.aimDirection, 0, input.reload);
        held = input.shoot;
        world.update(dt);
        world.takeEvents();
    }
    expect(world.players()[0].health < 100 && world.players()[1].position.x < 512,
           "Flame bot widens a bullet-sized gap into a body-sized passage and reaches a distant opponent");
}

void coveredRangeAdvantage()
{
    auto arena = Arena::create(MapId::RuinsSmall);
    World world(arena, 2);
    world.equip(2, WeaponKind::Automatic);
    BotController bot(arena, BotDifficulty::Hard, 42);
    bool held = false;
    for (int tick = 0; tick < 3600 && world.players()[0].health == 100; ++tick)
    {
        auto input = bot.update(observeBot(world, 2, 1), dt);
        world.setPlayerInput(2, {input.moveDirection, false, false, input.aimDirection, input.triggerHeld,
                                 false, input.jetpackHeld});
        if (input.jump || (input.shoot && !held) || input.reload)
            world.requestAction(2, input.jump, input.shoot && !held, input.aimDirection, 0, input.reload);
        held = input.shoot;
        world.update(dt);
        world.takeEvents();
    }
    expect(world.players()[0].health < 100,
           "A range advantage never keeps the bot behind cover with no viable firing line");
}

int main()
{
    try
    {
        supportPrediction();
        headEncounters();
        realPenetration();
        explosivesBothSlots();
        bodySizedOpening();
        coveredRangeAdvantage();
        std::cout << "Bot support, physical hits and material tactics passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
