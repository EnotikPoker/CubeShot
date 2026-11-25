#include "game/World.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;
constexpr float dt = 1.f / 120;

void expect(bool b, const char* m)
{
    if (!b)
        throw std::runtime_error(m);
}

Arena flat()
{
    return {{{0, 0}, {1200, 540}}, {{{0, 440}, {1200, 100}}}, {{100, 392}, {230, 392}}, MapId::Duel};
}

void step(World& w, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        w.update(dt);
}

Player& body(World& w, unsigned n)
{
    return const_cast<Player&>(w.players()[n]);
}

void attack(World& w, Vec2 aim = {1, 0})
{
    w.requestAction(1, false, true, aim);
    step(w, 36);
}

void defenses()
{
    World stance(flat(), 2);
    stance.equip(2, WeaponKind::Knife);
    Abilities a;
    a.add(AbilityKind::Stance);
    stance.setAbilities(2, a);
    body(stance, 1).aimDirection = {-1, 0};
    attack(stance);
    expect(body(stance, 1).health == 84, "Front stance reduces actual bullet damage by 20 percent");
    body(stance, 1).aimDirection = {1, 0};
    attack(stance);
    expect(body(stance, 1).health == 64, "Rear attack bypasses directional stance");
    World temper(flat(), 2);
    a = {};
    a.add(AbilityKind::Tempering);
    temper.setAbilities(2, a);
    attack(temper);
    attack(temper);
    expect(body(temper, 1).health == 64 && body(temper, 1).temperCooldown > 0,
           "Tempering starts after first HP damage and mitigates second hit");
    World execute(flat(), 2);
    a = {};
    a.add(AbilityKind::Executioner);
    execute.setAbilities(1, a);
    execute.setHealth(2, 25);
    attack(execute);
    expect(body(execute, 1).health == 2, "Executioner boosts direct damage only below HP threshold");
    World push(flat(), 2);
    a = {};
    a.add(AbilityKind::Impulse);
    a.add(AbilityKind::Impulse);
    push.setAbilities(1, a);
    push.requestAction(1, false, true, {1, 0});
    for (int n = 0; n < 25 && push.players()[1].health == 100; ++n)
        push.update(dt);
    expect(push.players()[1].pushX > 140, "Impact passive adds real physical impulse");
}

void resources()
{
    Player p;
    auto arena = flat();
    Abilities a;
    a.add(AbilityKind::Economy);
    p.setAbilities(a);
    p.weapon.equip(WeaponKind::Pistol);
    for (int n = 0; n < 4; ++n)
    {
        p.weapon.cooldown = 0;
        p.requestAction(false, true, {1, 0});
        expect(p.takeShot().has_value(), "Resource fixture fires");
    }
    expect(p.weapon.ammo == 9, "Every fourth shot is free");
    p.weapon.ammo = 1;
    p.economyCounter = 3;
    p.weapon.cooldown = 0;
    p.requestAction(false, true, {1, 0});
    p.takeShot();
    expect(p.weapon.ammo == 1 && !p.weapon.reloading, "Free last round does not start an unnecessary reload");
    a = {};
    a.add(AbilityKind::FreshMagazine);
    p.setAbilities(a);
    p.weapon.ammo = 5;
    p.weapon.startReload();
    for (int n = 0; n < 180; ++n)
        p.update(dt, arena);
    expect(p.freshMagazine, "Completing reload arms fresh-magazine bonus");
    p.requestAction(false, true, {1, 0});
    auto shot = p.takeShot();
    expect(shot && shot->power == 1.25f && !p.freshMagazine,
           "Fresh magazine boosts only next successful shot");
    Player reload;
    reload.weapon.ammo = 1;
    reload.weapon.startReload();
    reload.update(.25f, arena);
    const float remaining = reload.weapon.reloadRemaining;
    reload.setActives({ActiveKind::Overdrive, ActiveKind::None});
    expect(reload.activate(0, arena), "Overdrive activates during reload");
    reload.update(dt, arena);
    expect(std::abs(reload.weapon.reloadRemaining - (remaining / 1.25f - dt)) < .001f,
           "Overdrive accelerates an ongoing reload without restarting it");
    reload.overdrive = dt / 2;
    const float accelerated = reload.weapon.reloadRemaining;
    reload.update(dt, arena);
    expect(std::abs(reload.weapon.reloadRemaining - (accelerated * 1.25f - dt)) < .001f,
           "Ending overdrive preserves reload progress at the ordinary rate");
    Player air;
    air.position = {200, 200};
    Abilities mobility;
    mobility.add(AbilityKind::AirManeuver);
    mobility.add(AbilityKind::AirManeuver);
    air.setAbilities(mobility);
    air.applyInput({1});
    air.update(dt, arena);
    expect(std::abs(air.velocity.x - 364) < .1f, "Air maneuver changes real movement velocity");
}

void echo()
{
    auto a = flat();
    a.spawnPoints[1] = {190, 392};
    World w(a, 2);
    w.equip(1, WeaponKind::Katana);
    Abilities traits;
    traits.add(AbilityKind::Echo);
    traits.add(AbilityKind::Vampirism);
    w.setAbilities(1, traits);
    w.setHealth(1, 50);
    Abilities health;
    health.add(AbilityKind::Health);
    health.add(AbilityKind::Health);
    w.setAbilities(2, health);
    for (int n = 0; n < 3; ++n)
    {
        w.requestAction(1, false, true, {1, 0});
        step(w, 45);
    }
    expect(body(w, 1).health == 99, "Every third melee hit adds exactly 35 percent echo");
    expect(body(w, 0).health == 63, "Echo does not recursively trigger vampirism");
}

int main()
{
    try
    {
        defenses();
        resources();
        echo();
        std::cout << "Expansion passive interactions passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
