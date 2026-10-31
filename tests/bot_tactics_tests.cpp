#include "game/BotController.h"
#include "game/BotMatch.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

namespace
{
constexpr float dt = 1.f / 120;

void expect(bool b, const char* reason)
{
    if (!b)
        throw std::runtime_error(reason);
}

Arena flat(float x = 100, float y = 900)
{
    return {{{0, 0}, {1200, 540}}, {{{0, 440}, {1200, 100}}}, {{x, 392}, {y, 392}}, MapId::Duel};
}

void tick(World& w, BotController& bot, bool& held)
{
    const auto input = bot.update(observeBot(w, 2, 1), dt);
    expect(std::isfinite(input.aimDirection.x) && std::isfinite(input.aimDirection.y) &&
               std::abs(input.moveDirection) <= 1,
           "Every weapon matchup generates finite legal controls");
    w.setPlayerInput(2, {input.moveDirection, false, false, input.aimDirection, input.triggerHeld});
    if (input.jump || (input.shoot && !held))
        w.requestAction(2, input.jump, input.shoot && !held, input.aimDirection);
    held = input.shoot;
    w.update(dt);
}

void knowledgeAndMatrix()
{
    auto a = flat();
    for (unsigned own = 0; own < weaponCount; ++own)
        for (unsigned other = 0; other < weaponCount; ++other)
        {
            World w(a, 2);
            w.equip(1, static_cast<WeaponKind>(other));
            w.equip(2, static_cast<WeaponKind>(own));
            BotController bot(a, BotDifficulty::Hard, own * 31 + other + 1);
            bool held = false;
            expect(observeBot(w, 2, 1).target->weapon == static_cast<WeaponKind>(other),
                   "Enemy weapon is part of visible observation");
            for (int n = 0; n < 40; ++n)
                tick(w, bot, held);
            const auto profile = botWeaponProfile(static_cast<WeaponKind>(own));
            expect(profile.maximum > profile.minimum && profile.minimum >= 0,
                   "All fourteen weapons have valid tactical reach");
        }
    expect(botWeaponProfile(WeaponKind::Sniper).maximum > botWeaponProfile(WeaponKind::Shotgun).maximum * 3,
           "Sniper and shotgun cannot share a pistol engagement distance");
}

void fireEscape()
{
    auto a = flat(100, 600);
    World world(a, 2);
    BotController bot(a, BotDifficulty::Hard, 7);
    Player p = world.players()[1];
    const FirePatch fire{1, 1, {624, 439}, 3, 34};
    bool jumped = false;
    for (int n = 0; n < 120; ++n)
    {
        auto observation = observeBot(world, 2, 1);
        observation.self = p;
        observation.fires = {fire};
        auto input = bot.update(observation, dt);
        jumped = jumped || input.jump;
        p.applyInput({input.moveDirection, false, false, input.aimDirection});
        p.requestAction(input.jump, false, input.aimDirection);
        p.update(dt, a);
    }
    expect(std::abs(p.position.x - 600) > 40 || p.position.y < 340,
           "Bot escapes observed fire rather than standing in it");
    expect(std::isfinite(p.position.y), "Fire avoidance uses normal grounded/jump physics");
}

void enemyMeleeSpacing()
{
    auto simulate = [](WeaponKind kind)
    {
        auto a = flat(180, 350);
        World world(a, 2);
        world.equip(1, kind);
        world.equip(2, WeaponKind::Smg);
        BotController bot(a, BotDifficulty::Hard, 77);
        for (int n = 0; n < 70; ++n)
        {
            auto input = bot.update(observeBot(world, 2, 1), dt);
            world.setPlayerInput(2, {input.moveDirection, false, false, input.aimDirection, false});
            if (input.jump)
                world.requestAction(2, true, false, input.aimDirection);
            world.update(dt);
        }
        return world.players()[1].position.x;
    };
    const float katana = simulate(WeaponKind::Katana), pistol = simulate(WeaponKind::Pistol);
    expect(katana > pistol + 5, "Ranged bot creates extra separation from enemy melee weapon");
}

void enemyFirearmSpacing()
{
    auto simulate = [](WeaponKind kind, bool overheated)
    {
        auto arena = flat(180, 370);
        World world(arena, 2);
        world.equip(1, kind);
        world.equip(2, WeaponKind::Automatic);
        BotController bot(arena, BotDifficulty::Hard, 77);
        for (int n = 0; n < 70; ++n)
        {
            auto observation = observeBot(world, 2, 1);
            observation.target->overheated = overheated;
            auto input = bot.update(observation, dt);
            world.setPlayerInput(2, {input.moveDirection, false, false, input.aimDirection, false});
            if (input.jump)
                world.requestAction(2, true, false, input.aimDirection);
            world.update(dt);
        }
        return world.players()[1].position.x;
    };
    expect(simulate(WeaponKind::Smg, false) > simulate(WeaponKind::Pistol, false) + 20,
           "Automatic exploits its range advantage against SMG rather than using the pistol matchup");
    expect(simulate(WeaponKind::Flamethrower, false) > simulate(WeaponKind::Flamethrower, true) + 20,
           "A visible enemy overheat opens an approach window instead of preserving unnecessary distance");
}

void movingMelee()
{
    for (auto kind : {WeaponKind::Bat, WeaponKind::Katana, WeaponKind::Knife})
    {
        auto a = flat(180, 700);
        World world(a, 2);
        world.equip(2, kind);
        BotController bot(a, BotDifficulty::Hard, 4);
        bool held = false;
        bool hurt = false;
        for (int n = 0; n < 120 * 12 && world.players()[1].alive(); ++n)
        {
            world.setPlayerInput(1, {(n / 90) % 2 ? .35f : -.35f, false, false, {1, 0}});
            tick(world, bot, held);
            if (world.players()[0].health < 100)
            {
                hurt = true;
                break;
            }
        }
        expect(hurt, "Melee bot closes and damages a moving opponent instead of stopping at pistol distance");
    }
}

void sniperAndSplash()
{
    auto arena = flat(100, 1000);
    World world(arena, 2);
    world.equip(1, WeaponKind::Sniper);
    BotController bot(arena, BotDifficulty::Hard, 42);
    bool held = false;
    world.requestAction(1, false, true, {1, 0});
    for (int n = 0; n < 120; ++n)
    {
        const auto origin = world.players()[0].weaponOrigin(), target = world.players()[1].weaponOrigin();
        float length = std::hypot(target.x - origin.x, target.y - origin.y);
        world.setPlayerInput(
            1, {0, false, false, {(target.x - origin.x) / length, (target.y - origin.y) / length}, true});
        tick(world, bot, held);
    }
    expect(world.players()[1].alive() && world.players()[1].position.x < 980,
           "Hard bot advances and evades a prepared long-range sniper shot instead of waiting at spawn");
    BotController danger(arena, BotDifficulty::Hard, 7);
    Player self;
    self.id = 2;
    self.position = {600, 392};
    self.grounded = true;
    Bullet rocket;
    rocket.id = 1;
    rocket.weapon = WeaponKind::Rpg;
    rocket.shooterId = 1;
    rocket.position = {450, 420};
    rocket.velocity = {300, 90};
    rocket.damage = 100;
    rocket.acceleration = 25;
    rocket.lifetime = 4;
    bool reacted = false;
    for (int n = 0; n < 24; ++n)
    {
        auto input = danger.update({self, {}, {rocket}, {}}, dt);
        reacted = reacted || input.jump || input.moveDirection > 0;
        self.applyInput({input.moveDirection, false, false, input.aimDirection});
        self.requestAction(input.jump, false, input.aimDirection);
        self.update(dt, arena);
        rocket.update(dt);
    }
    expect(
        reacted,
        "Bot reacts to rocket splash near the floor even when the rocket never directly intersects its body");
}

void liveRoundChange()
{
    BotMatch game(MapId::Duel, BotDifficulty::Easy, "Test", 42);
    const auto arena = Arena::createDuel();
    bool rolled = false, secondShot = false, secondHit = false, thirdShot = false;
    for (int tick = 0; tick < 120 * 150 && !thirdShot; ++tick)
    {
        const auto state = game.snapshot();
        if (state.phase == MatchPhase::Armory)
            game.chooseWeapon(false);
        game.control({});
        if (state.phase == MatchPhase::Round && state.round == 1)
        {
            const auto& target = state.players[1];
            BotTarget observed{target.position, target.velocity, target.size, target.grounded,
                               target.weapon.kind};
            if (auto aim = botAim(state.players[0], observed, arena);
                aim && state.players[0].weapon.cooldown <= 0)
                game.action(false, true, aim->direction);
        }
        if (state.phase == MatchPhase::Round && state.round >= 2)
        {
            rolled = rolled || state.players[1].weapon.kind != WeaponKind::Pistol;
            secondHit = secondHit || state.players[0].health < 100;
        }
        game.update(dt);
        for (const auto& event : game.takeEvents())
            if (event.kind == EventKind::Shot && event.actor == 2 && event.weapon != WeaponKind::Pistol)
            {
                secondShot = secondShot || event.round >= 2;
                thirdShot = thirdShot || event.round >= 3;
            }
    }
    expect(rolled && secondShot && secondHit && thirdShot,
           "A real bot match loses the pistol round, rolls a new weapon, attacks in round two and correctly "
           "reuses it after the next reset");
}

void resetAndSafety()
{
    auto a = flat(500, 600);
    World world(a, 2);
    world.equip(2, WeaponKind::Rpg);
    BotController bot(a, BotDifficulty::Hard, 2);
    for (int n = 0; n < 60; ++n)
    {
        auto input = bot.update(observeBot(world, 2, 1), dt);
        expect(!input.shoot, "RPG is not fired directly into a nearby self-damaging target");
    }
    bot.reset();
    world.equip(2, WeaponKind::Knife);
    bool held = false;
    for (int n = 0; n < 120; ++n)
        tick(world, bot, held);
    expect(
        world.players()[0].health < 100,
        "Changing from RPG to knife after a round reset restores the appropriate attack and trigger state");
}
} // namespace

int main()
{
    try
    {
        knowledgeAndMatrix();
        fireEscape();
        enemyMeleeSpacing();
        enemyFirearmSpacing();
        movingMelee();
        sniperAndSplash();
        resetAndSafety();
        liveRoundChange();
        std::cout
            << "196 weapon matchups, fire, moving melee, splash and live round-two/three roulette passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
