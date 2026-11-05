#include "game/Abilities.h"
#include "game/BotController.h"
#include "game/BotNavigation.h"
#include "game/Collision.h"
#include "game/Match.h"
#include "game/Prediction.h"
#include "network/Protocol.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
using namespace cubeshot;

namespace
{
constexpr float dt = 1.f / 120;

void expect(bool b, const char* text)
{
    if (!b)
        throw std::runtime_error(text);
}

Arena flat()
{
    return {{{0, 0}, {2000, 1000}}, {{{0, 900}, {2000, 100}}}, {{200, 852}, {400, 852}}, MapId::Duel};
}

void advance(World& w, int ticks)
{
    for (int i = 0; i < ticks; ++i)
        w.update(dt);
}

void catalog()
{
    Abilities a;
    for (unsigned i = 0; i < abilityCount; ++i)
        expect(a.add(static_cast<AbilityKind>(i)), "Every initial ability can be rolled");
    expect(!a.add(AbilityKind::SmallBody) && !a.add(AbilityKind::LargeBullets) &&
               !a.add(AbilityKind::Jetpack) && !a.add(AbilityKind::Recoil),
           "Unique abilities cannot stack");
    for (int i = 0; i < 2; ++i)
    {
        expect(a.add(AbilityKind::Speed), "Speed stacks to three");
        expect(a.add(AbilityKind::Jump), "Jump stacks to three");
    }
    expect(!a.add(AbilityKind::Speed) && !a.add(AbilityKind::Jump), "Movement caps exclude useless outcomes");
    expect(std::abs(a.speed() - 1.9f) < .001f && std::abs(a.jump() * a.jump() - 1.9f) < .001f,
           "Jump bonus changes height, not a squared velocity bonus");
    for (int i = 0; i < 20; ++i)
    {
        a.add(AbilityKind::Health);
        a.add(AbilityKind::Armor);
        a.add(AbilityKind::Magazine);
        a.add(AbilityKind::FastReload);
    }
    expect(a.health() == 1150 && a.shields() == 42 && a.valid(), "Repeatable upgrades retain every level");
    for (auto k : a.available())
        expect(k != AbilityKind::Speed && k != AbilityKind::Jump && k != AbilityKind::SmallBody,
               "Capped entries are removed from the draw pool");
}

void bodyAndWeapons()
{
    Player p;
    p.id = 1;
    p.position = {200, 852};
    auto foot = p.position.y + p.size.y, centre = p.position.x + p.size.x / 2;
    Abilities a;
    a.add(AbilityKind::SmallBody);
    a.add(AbilityKind::Health);
    a.add(AbilityKind::Magazine);
    a.add(AbilityKind::FastReload);
    a.add(AbilityKind::Recoil);
    a.add(AbilityKind::LargeBullets);
    p.setAbilities(a);
    expect(p.size.x == 24 && p.position.y + p.size.y == foot && p.position.x + p.size.x / 2 == centre &&
               p.health == 150,
           "Shrinking keeps feet and centre aligned");
    for (auto local : {Vec2{0, 0}, Vec2{24, 26.4f}, Vec2{48, 48}, Vec2{12, 37}})
    {
        auto back = p.bodyLocal(p.bodyPoint(local));
        expect(std::hypot(back.x - local.x, back.y - local.y) < .001f,
               "Scaled body and impact coordinates are inverse");
    }
    auto shape = p.bodyContour();
    expect(polygonArea(shape) < 600 && polygonArea(shape) > 500,
           "Projectile contour really shrinks to one quarter area");
    for (unsigned k = 0; k < weaponCount; ++k)
    {
        p.weapon.equip(static_cast<WeaponKind>(k));
        auto d = weaponDefinition(p.weapon.kind);
        int expected = d.capacity ? d.capacity + static_cast<int>(std::ceil(d.capacity * .25f)) : 0;
        expect(p.weapon.capacity() == expected && p.weapon.ammo == expected,
               "Magazine upgrade follows every new weapon");
        expect(std::abs(p.weapon.reloadDuration() - d.reloadTime * .8f) < .001f,
               "Reload rate follows every new weapon");
    }
    p.weapon.equip(WeaponKind::Shotgun);
    p.grounded = true;
    p.requestAction(false, true, {0, 1});
    auto b = p.takeShot();
    expect(b && b->collisionRadius == 4.5f && p.velocity.y < -674 && p.weapon.ammo == 7,
           "Big pellets, one shell and amplified recoil combine");
    Debris debris;
    p.health = 0;
    debris.spawn(p, {}, {1, 0}, WeaponKind::Pistol, 20);
    auto corpse = debris.corpses()[0].body;
    expect(corpse.visualScale == .5f && corpse.size.x < 26 && corpse.hasEye,
           "Small corpse and eye retain the physical scale");
}

void armorAndFire()
{
    World w(flat(), 2);
    w.equip(1, WeaponKind::Shotgun);
    Abilities armor;
    armor.add(AbilityKind::Armor);
    w.setAbilities(2, armor);
    Abilities incendiary;
    incendiary.add(AbilityKind::Incendiary);
    w.setAbilities(1, incendiary);
    for (unsigned shot = 0; shot < 3; ++shot)
    {
        w.requestAction(1, false, true, {1, 0});
        advance(w, 130);
        if (shot < 2)
            expect(w.players()[1].health == 100 && w.players()[1].shields == 1 - shot &&
                       w.players()[1].burning == 0,
                   "One shield absorbs an entire shotgun volley and ignition");
    }
    expect(w.players()[1].health < 100, "Unshielded damage resumes after both shield charges");
    auto a = flat();
    a.spawnPoints[1] = {260, 852};
    World direct(a, 2);
    direct.equip(1, WeaponKind::Flamethrower);
    direct.setAbilities(2, armor);
    direct.requestAction(1, false, true, {1, 0});
    advance(direct, 120);
    expect(direct.players()[1].health == 100 && direct.players()[1].shields == 1 &&
               direct.players()[1].burning == 0 && direct.fires().empty(),
           "A shielded flame hit cannot secretly ignite the ground under its victim");
    World fire(a, 2);
    fire.equip(1, WeaponKind::Flamethrower);
    fire.setAbilities(2, armor);
    fire.setPlayerInput(1, {0, false, false, {.7071f, .7071f}});
    fire.requestAction(1, false, true, {.7071f, .7071f});
    advance(fire, 240);
    if (fire.players()[1].health == 100 || fire.players()[1].shields != 2)
        std::cerr << "Fire fixture hp=" << fire.players()[1].health
                  << " shields=" << fire.players()[1].shields << " fires=" << fire.fires().size() << "\n";
    expect(fire.players()[1].health < 100 && fire.players()[1].shields == 2,
           "Surface burning damages health without consuming shields per tick");
    World self(flat(), 2);
    self.setAbilities(1, armor);
    self.equip(1, WeaponKind::Rpg);
    self.requestAction(1, false, true, {0, 1});
    advance(self, 2);
    expect(!self.players()[0].alive() && self.players()[0].shields == 2,
           "Armor does not remove lethal rocket self damage");
}

void largerContact()
{
    auto a = flat();
    a.platforms.push_back({{400, 886}, {2, 12}});
    bool contact[2]{};
    for (int big = 0; big < 2; ++big)
    {
        World w(a, 1);
        if (big)
        {
            Abilities x;
            x.add(AbilityKind::LargeBullets);
            w.setAbilities(1, x);
        }
        w.requestAction(1, false, true, {1, 0});
        for (int t = 0; t < 100; ++t)
        {
            w.update(dt);
            for (auto e : w.takeEvents())
                if (e.kind == EventKind::SurfaceImpact && e.position.x > 390 && e.position.x < 425)
                    contact[big] = true;
        }
    }
    expect(!contact[0] && contact[1], "Large bullet radius changes contact, not only the picture");
}

void jetpackAndNavigation()
{
    auto a = flat();
    a.bounds.size.y = 3000;
    a.platforms = {{{0, 2900}, {2000, 100}}};
    Player p;
    p.position = {500, 2852};
    p.grounded = true;
    Abilities abilities;
    abilities.add(AbilityKind::Jetpack);
    p.setAbilities(abilities);
    for (int i = 0; i < 300; ++i)
    {
        PlayerInput in;
        in.jump = i == 0;
        in.jetpackHeld = true;
        p.applyInput(in);
        p.update(dt, a);
    }
    expect(p.position.y < 2000 && p.jetpackFuel < .001f,
           "Holding jump gives two seconds of bounded airborne thrust");
    for (int i = 0; i < 1000 && !p.grounded; ++i)
    {
        p.applyInput({});
        p.update(dt, a);
    }
    expect(p.grounded && p.jetpackFuel == 2, "Landing restores fuel even before the next jump press");
    p.applyInput({0, true, false, {1, 0}, false, false, true});
    p.update(dt, a);
    expect(p.jetpackActive && p.jetpackFuel > 1.98f, "A fresh jump has the restored fuel");
    a = flat();
    a.platforms.push_back({{750, 700}, {240, 20}});
    auto ordinary = BotNavigation::forArena(a);
    auto flight = BotNavigation::forArena(a, abilities);
    expect(ordinary->route({500, 852}, {800, 652}).empty(), "High ledge is beyond the normal jump");
    auto route = flight->route({500, 852}, {800, 652});
    expect(!route.empty() && std::any_of(route.begin(), route.end(), [](auto e) { return e.jetpack; }),
           "Jetpack routes are verified by actual physics");
}

void wireAndPrediction()
{
    Match match(false, 42);
    match.connect(1);
    match.connect(2);
    match.setReady(1, true);
    match.setReady(2, true);
    for (int i = 0; i < 361; ++i)
        match.update(dt);
    World world(Arena::createDuel(), 2);
    Abilities abilities;
    for (unsigned i = 0; i < abilityCount; ++i)
        abilities.add(static_cast<AbilityKind>(i));
    abilities.add(AbilityKind::Health);
    abilities.add(AbilityKind::Speed);
    world.setAbilities(1, abilities);
    world.equip(1, WeaponKind::Minigun);
    auto state = match.snapshot();
    Prediction prediction;
    for (int tick = 0; tick < 360; ++tick)
    {
        state.players = world.players();
        state.bullets = world.bullets();
        state.abilities[0] = abilities;
        state.inputAck[0] = prediction.sequence();
        state.actionAck[0] = tick > 10 ? 1 : 0;
        auto decoded = network::decodeSnapshot(network::encodeSnapshot(state));
        expect(bool(decoded), "Upgraded snapshots validate and roundtrip");
        prediction.reconcile(*decoded, 1);
        PlayerInput in;
        in.aimDirection = {0, 1};
        in.triggerHeld = tick > 60 && tick < 250;
        in.jetpackHeld = tick >= 10 && tick < 150;
        if (tick == 10)
        {
            world.requestAction(1, true, false, {0, 1}, 1);
            prediction.action(1, true, false, {0, 1});
        }
        world.setPlayerInput(1, in);
        world.update(dt, false);
        prediction.advance(in);
        auto own = *prediction.player();
        auto actual = world.players()[0];
        expect(std::hypot(own.position.x - actual.position.x, own.position.y - actual.position.y) < .1f &&
                   std::abs(own.jetpackFuel - actual.jetpackFuel) < .001f &&
                   own.weapon.ammo == actual.weapon.ammo,
               "Prediction agrees for small body, jetpack, recoil and modified magazines");
    }
    auto wire = network::encodeSnapshot(state);
    wire.mutable_players(0)->mutable_abilities()->set_levels(1, 4);
    expect(!network::decodeSnapshot(wire), "Reject impossible speed levels");
    wire = network::encodeSnapshot(state);
    wire.mutable_players(0)->set_shields(999);
    expect(!network::decodeSnapshot(wire), "Reject shields beyond owned armor");
    wire = network::encodeSnapshot(state);
    wire.mutable_players(0)->set_jetpack_fuel(std::numeric_limits<float>::quiet_NaN());
    expect(!network::decodeSnapshot(wire), "Reject nonfinite jetpack fuel");
    GameEvent e;
    e.id = 1;
    e.round = 1;
    e.actor = 1;
    e.target = 2;
    e.kind = EventKind::ShieldBlock;
    e.damage = 20;
    e.direction = {1, 0};
    expect(network::decodeEvents(network::encodeEvents({e})).has_value(),
           "Shield impact events validate on both peers");
}

void airborneTarget()
{
    World world(flat(), 2);
    Abilities jet;
    jet.add(AbilityKind::Jetpack);
    world.setAbilities(1, jet);
    world.equip(2, WeaponKind::Rifle);
    for (int i = 0; i < 30; ++i)
    {
        PlayerInput input;
        input.jump = i == 0;
        input.jetpackHeld = true;
        world.setPlayerInput(1, input);
        world.update(dt);
    }
    auto seen = observeBot(world, 2, 1);
    expect(seen.target && seen.target->jetpackActive && seen.target->jetpackFuel < 2,
           "Bot observes visible thrust and remaining fuel");
    auto aim = botAim(seen.self, *seen.target, world.arena());
    expect(bool(aim), "A rifle can intercept an ascending jetpack target");
    world.requestAction(2, false, true, aim->direction);
    for (int i = 0; i < 240 && world.players()[0].health == 100; ++i)
    {
        PlayerInput input;
        input.jetpackHeld = true;
        world.setPlayerInput(1, input);
        world.update(dt);
    }
    expect(world.players()[0].health < 100,
           "Interception accounts for continued visible thrust rather than predicting a free fall");
}

void roulette()
{
    Match m(false, 42);
    m.connect(1);
    m.connect(2);
    m.setReady(1, true);
    m.setReady(2, true);
    Abilities expected;
    for (unsigned win = 1; win <= 5; ++win)
    {
        for (int i = 0; i < 400 && m.phase() == MatchPhase::Countdown; ++i)
            m.update(dt);
        expect(m.phase() == MatchPhase::Round, "The next round starts after the upgrade countdown");
        const bool swapped = m.world().players()[0].position.x > m.world().players()[1].position.x;
        m.setInput(1, {swapped ? -1.f : 1.f});
        m.setInput(2, {swapped ? 1.f : -1.f});
        for (int i = 0; i < 120; ++i)
            m.update(dt);
        m.setInput(1, {});
        m.setInput(2, {});
        unsigned sequence = 0;
        for (int shot = 0; shot < 8 && m.phase() == MatchPhase::Round; ++shot)
        {
            auto from = m.world().players()[0].weaponOrigin(), to = m.world().players()[1].weaponOrigin();
            m.action(1, m.round(), ++sequence, false, true, {to.x - from.x, to.y - from.y});
            for (int i = 0; i < 70; ++i)
                m.update(dt);
        }
        expect(!m.world().players()[1].alive(), "Pistol duel fixture completes the round");
        for (int i = 0; i < 600 && m.phase() == MatchPhase::RoundEnd; ++i)
            m.update(dt);
        auto state = m.snapshot();
        expect(state.abilities[0] == expected, "Earlier upgrades survive later deaths and rounds");
        if (win == 5)
        {
            expect(state.phase == MatchPhase::Finished,
                   "Final victory opens the result instead of another roll");
            break;
        }
        expect(state.phase == MatchPhase::Armory, "A completed round offers a shared upgrade choice");
        expect(!m.chooseUpgrade(1, UpgradeKind::Ability, state.round - 1, state.selectionRevision),
               "Reject previous-round upgrade packets");
        expect(m.chooseUpgrade(1, UpgradeKind::Ability, state.round, state.selectionRevision),
               "Ability wheel starts once");
        auto rolled = m.snapshot().rolledAbility[0];
        expect(expected.canAdd(rolled), "Server excludes capped ability outcomes");
        expected.add(rolled);
        expect(!m.chooseWeapon(1, true, state.round, state.selectionRevision) &&
                   !m.chooseWeapon(1, false, state.round, state.selectionRevision),
               "Ability choice prevents weapon reroll and switching to continue");
        m.chooseWeapon(2, false, state.round, state.selectionRevision);
        for (int i = 0; i < 400 && m.phase() == MatchPhase::Armory; ++i)
            m.update(dt);
        auto next = m.snapshot();
        expect(next.abilities[0] == expected && next.players[0].abilities == expected &&
                   next.loadout[0] == WeaponKind::Pistol,
               "Abilities accumulate across selections without replacing the weapon");
    }
    expect(expected != Abilities{}, "Match fixture earned several actual upgrades");
    m.setReady(1, true);
    m.setReady(2, true);
    expect(m.snapshot().abilities[0] == Abilities{} && m.world().players()[0].health == 100 &&
               m.world().players()[0].weapon.capacity() == 12,
           "Rematch resets abilities, HP and weapon modifiers");
    m.disconnect(2);
    expect(m.snapshot().abilities[0] == Abilities{} && m.snapshot().players[0].health == 100,
           "Disconnect resets accumulated upgrades with the match");
}

} // namespace

int main()
{
    try
    {
        catalog();
        bodyAndWeapons();
        armorAndFire();
        largerContact();
        jetpackAndNavigation();
        wireAndPrediction();
        airborneTarget();
        roulette();
        std::cout << "Ability growth, armor, geometry, jetpack, roulette and wire prediction passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
