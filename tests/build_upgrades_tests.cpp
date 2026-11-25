#include "game/AttackQueries.h"
#include "game/AttackStats.h"
#include "game/BoomerangMotion.h"
#include "game/BotUpgrades.h"
#include "game/ProjectileMotion.h"
#include "game/World.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

void expect(bool b, const char* message)
{
    if (!b)
        throw std::runtime_error(message);
}

constexpr float dt = 1.f / 120;

Arena flat(float gap = 50)
{
    return {{{0, 0}, {1200, 540}}, {{{0, 440}, {1200, 100}}}, {{100, 392}, {100 + gap, 392}}, MapId::Duel};
}

void meleeMechanics()
{
    auto arena = flat();
    World combo(arena, 2);
    combo.equip(1, WeaponKind::Knife);
    Abilities a;
    a.add(AbilityKind::Combo);
    combo.setAbilities(1, a);
    int previous = 100;
    for (int hit = 0; hit < 4; ++hit)
    {
        combo.requestAction(1, false, true, {1, 0});
        combo.update(dt);
        int loss = previous - combo.players()[1].health;
        expect(loss == int(std::lround(15 * (1 + .1f * hit))),
               "Combo increases real consecutive HP hits by 10/20/30 percent");
        previous = combo.players()[1].health;
        for (int n = 0; n < 30; ++n)
            combo.update(dt);
    }
    combo.requestAction(1, false, true, {-1, 0});
    combo.update(dt);
    expect(combo.players()[0].comboHits == 0, "A missed swing resets combo");
    World bleed(arena, 2);
    bleed.equip(1, WeaponKind::Knife);
    a = {};
    a.add(AbilityKind::Bleeding);
    bleed.setAbilities(1, a);
    Abilities resistant;
    resistant.add(AbilityKind::FireResistance);
    bleed.setAbilities(2, resistant);
    bleed.requestAction(1, false, true, {1, 0});
    bleed.update(dt);
    int initial = bleed.players()[1].health;
    for (int n = 0; n < 365; ++n)
        bleed.update(dt);
    expect(bleed.players()[1].health == initial - 12,
           "Bleeding deals three physical ticks independent of heat resistance");
    Player p;
    p.id = 1;
    p.position = {100, 392};
    p.weapon.equip(WeaponKind::Knife);
    a = {};
    a.add(AbilityKind::Lunge);
    p.setAbilities(a);
    p.requestAction(false, true, {1, 0});
    expect(p.takeShot().has_value() && p.pushX > 119, "Melee lunge adds physical impulse");
    float push = p.pushX;
    p.weapon.cooldown = 0;
    p.requestAction(false, true, {1, 0});
    p.takeShot();
    expect(p.pushX == push, "Rapid attacks cannot bypass lunge cooldown");
    Player victim;
    victim.position = {200, 392};
    auto origin = p.weaponOrigin();
    expect(!meleeContact(origin, {1, 0}, attackStats(WeaponKind::Knife, {}).reach, victim, arena),
           "Base knife cannot reach distant contour");
    a = {};
    a.add(AbilityKind::Range);
    a.add(AbilityKind::Range);
    expect(meleeContact(origin, {1, 0}, attackStats(WeaponKind::Knife, a).reach, victim, arena).has_value(),
           "Range upgrades extend actual melee contact");
    Abilities small;
    small.add(AbilityKind::SmallBody);
    small.add(AbilityKind::Range);
    expect(attackStats(WeaponKind::Knife, small).reach == 65,
           "Small body does not shrink earned melee reach");
    victim.position = {120, 260};
    origin = {124, 360};
    expect(!meleeContact(origin, {1, 0}, 100, victim, arena), "Base horizontal swing cannot hit steep angle");
    a = {};
    for (int n = 0; n < 3; ++n)
        a.add(AbilityKind::WideSwing);
    expect(meleeContact(origin, {1, 0}, 100, victim, arena, attackStats(WeaponKind::Knife, a).halfAngle)
               .has_value(),
           "Wide swing reaches targets above within the expanded arc");
}

void boomerangAuthority()
{
    auto arena = flat(130);
    World world(arena, 2);
    world.equip(1, WeaponKind::FireBoomerang);
    Abilities perks;
    perks.add(AbilityKind::ReturnStrike);
    world.setAbilities(1, perks);
    world.requestAction(1, false, true, {1, 0});
    bool out = false, back = false;
    for (int n = 0; n < 180; ++n)
    {
        world.update(dt);
        for (auto e : world.takeEvents())
            if (e.kind == EventKind::Hit && e.actor == 1 && e.target == 2 &&
                e.weapon == WeaponKind::FireBoomerang)
            {
                if (!out)
                {
                    expect(e.damage == 20, "Outbound boomerang base damage");
                    out = true;
                }
                else
                {
                    expect(e.damage == 25, "Return bonus is applied exactly once to real damage");
                    back = true;
                }
            }
    }
    expect(out && back, "Boomerang hits on both distinct legs");
    World empty(flat(900), 2);
    empty.equip(1, WeaponKind::FireBoomerang);
    empty.setAbilities(1, perks);
    empty.requestAction(1, false, true, {1, 0});
    empty.update(dt);
    expect(empty.bullets().size() == 1, "Shared motion fixture launches one boomerang");
    auto query = empty.bullets()[0];
    for (int n = 0; n < 100; ++n)
    {
        bool flying = steerBoomerang(query, empty.players()[0].weaponOrigin()) &&
                      advanceProjectileTerrain(query, arena, dt);
        empty.update(dt);
        expect(flying == !empty.bullets().empty(), "Authority and query agree about boomerang catch");
        if (!flying)
            break;
        auto actual = empty.bullets()[0];
        expect(std::hypot(query.position.x - actual.position.x, query.position.y - actual.position.y) < .01f,
               "Authority and query share boomerang phase trajectory");
    }
}

int main()
{
    try
    {
        meleeMechanics();
        boomerangAuthority();
        Abilities a;
        auto melee = a.available(WeaponKind::Knife);
        expect(std::find(melee.begin(), melee.end(), AbilityKind::Magazine) == melee.end(),
               "Melee roulette excludes magazine upgrades");
        expect(std::find(melee.begin(), melee.end(), AbilityKind::Bleeding) != melee.end(),
               "Melee roulette includes bleeding");
        a.add(AbilityKind::Range);
        a.add(AbilityKind::WideSwing);
        auto stats = attackStats(WeaponKind::Knife, a);
        expect(stats.reach == 65 && stats.halfAngle > .99f, "Melee reach and arc derive from real upgrades");
        Player p;
        p.setAbilities(a);
        p.weapon.equip(WeaponKind::FireBoomerang);
        a.add(AbilityKind::ProjectileSpeed);
        a.add(AbilityKind::ReturnStrike);
        a.add(AbilityKind::SecondCircuit);
        a.add(AbilityKind::TwinShot);
        p.setAbilities(a);
        auto b = p.shotPrototype({1, 0});
        expect(b.boomOutSpeed == 700 && b.boomReturnSpeed == 875 && b.boomRange == 385 && b.circuits == 1 &&
                   scatterShot(b).size() == 2,
               "Boomerang inherits range, speed, return and twin upgrades");
        b.returning = true;
        expect(b.impactDamage() == 25, "Return damage is applied without mutating outgoing damage");
        b.position = p.weaponOrigin();
        expect(steerBoomerang(b, p.weaponOrigin()) && !b.returning && b.circuits == 0,
               "Second circuit relaunches exactly once");
        b.returning = true;
        b.circuits = 1;
        expect(!steerBoomerang(b, p.weaponOrigin(), false), "Dead owner cannot start a second circuit");
        b.circuits = 0;
        expect(!steerBoomerang(b, p.weaponOrigin()), "Final return removes boomerang");
        Arena arena{{{0, 0}, {960, 540}}, {{{0, 440}, {960, 100}}}, {{100, 392}, {800, 392}}, MapId::Duel};
        arena.addTerrain({{400, 300}, {8, 8}}, Material::Metal);
        arena.rebuildTerrain();
        for (int i = 0; i < 4; ++i)
            arena.damageTerrain({404, 304}, 7, 20);
        expect(arena.terrain[0].health == 20, "Metal survives four pistol hits");
        arena.damageTerrain({404, 304}, 7, 20);
        expect(arena.terrain[0].health == 0, "Fifth direct pistol hit destroys metal cell");
        arena.restoreTerrain();
        arena.damageTerrain({404, 304}, 7, 20, false, true);
        expect(arena.terrain[0].health == 100,
               "Explosion damage does not receive the new direct-impact multiplier");
        Match match;
        auto state = match.snapshot();
        state.loadout[1] = WeaponKind::Sniper;
        state.players[1].weapon.equip(WeaponKind::Sniper);
        auto choice = chooseBotUpgrade(state, 2);
        expect(choice != UpgradeKind::Weapon,
               "Bot preserves a strong long-range weapon instead of changing it merely after defeat");
        state.loadout[1] = WeaponKind::Knife;
        state.players[1].weapon.equip(WeaponKind::Knife);
        std::deque<BotRoundStats> poor{{12, 0, 100, 0, .1f, 12}};
        expect(chooseBotUpgrade(state, 2, poor) == UpgradeKind::Weapon,
               "Bot replaces a melee build that repeatedly cannot reach its opponent");
        std::cout << "Specializations, compatible pools, boomerang, metal and upgrade decisions passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
