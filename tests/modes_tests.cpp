#include "game/Collision.h"
#include "game/Match.h"
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

// Deterministic placement builds a combat fixture; every death below is an actual World attack.
World& scene(Match& m)
{
    return const_cast<World&>(m.world());
}

Player& actor(Match& m, unsigned i)
{
    return const_cast<Player&>(m.world().players()[i]);
}

void tick(Match& m, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        m.update(dt);
}

void start(Match& m, MatchRules rules)
{
    m.connect(1);
    m.connect(2);
    expect(m.selectRules(1, rules, m.snapshot().selectionRevision), "Select mode rules");
    m.setReady(1, true);
    m.setReady(2, true);
    tick(m, 361);
    expect(m.phase() == MatchPhase::Round, "Start selected mode");
}

void kill(Match& m)
{
    actor(m, 1).spawnProtection = 0;
    actor(m, 0).position = {100, 392};
    actor(m, 1).position = {180, 392};
    actor(m, 0).clearInput();
    actor(m, 1).clearInput();
    scene(m).equip(1, WeaponKind::Pistol);
    scene(m).setHealth(2, 1);
    scene(m).requestAction(1, false, true, {1, 0});
    for (unsigned n = 0; n < 40 && actor(m, 1).alive(); ++n)
        m.update(dt);
    expect(!actor(m, 1).alive(), "Fixture kills by real weapon damage");
}

void race()
{
    for (unsigned length : {7, 14, 21})
    {
        Match m(false, 42);
        MatchRules r;
        r.mode = GameMode::ArmsRace;
        r.stages = length;
        start(m, r);
        auto chain = armsChain(length);
        expect(chain.size() == length && chain.back() == WeaponKind::Knife,
               "Defined race length and final knife");
        for (unsigned stage = 0; stage < length; ++stage)
        {
            expect(m.snapshot().loadout[0] == chain[stage], "Race equips current weapon stage");
            kill(m);
            expect(m.snapshot().score[0] == stage + 1, "Victory advances exactly one stage");
            tick(m, 481);
            if (stage + 1 == length)
            {
                expect(m.phase() == MatchPhase::Finished, "Last stage victory finishes race");
                break;
            }
            auto s = m.snapshot();
            expect(m.phase() == MatchPhase::Armory &&
                       !m.chooseUpgrade(1, UpgradeKind::Weapon, s.round, s.selectionRevision),
                   "Weapon roulette unavailable in race");
            m.chooseUpgrade(1, UpgradeKind::Continue, s.round, s.selectionRevision);
            m.chooseUpgrade(2, UpgradeKind::Continue, s.round, s.selectionRevision);
            tick(m, 363);
        }
    }
}

void chaos()
{
    Match m(false, 23);
    MatchRules r;
    r.mode = GameMode::Chaos;
    start(m, r);
    auto old = m.snapshot().mode.chaos;
    expect(old != ChaosKind::None, "Chaos selected before first round");
    auto modifier = chaosModifiers(old);
    expect(actor(m, 0).modifiers.gravity == modifier.gravity &&
               actor(m, 1).modifiers.movement == modifier.movement,
           "Same modifier on both participants");
    kill(m);
    tick(m, 481);
    auto s = m.snapshot();
    expect(s.mode.nextChaos != old && s.mode.nextChaos != ChaosKind::None,
           "Next condition announced without repeating current");
    m.chooseWeapon(1, false, s.round, s.selectionRevision);
    m.chooseWeapon(2, false, s.round, s.selectionRevision);
    tick(m, 363);
    expect(m.snapshot().mode.chaos == s.mode.nextChaos, "Announced condition applies next round");
}

void control()
{
    Match m(false, 42);
    MatchRules r;
    r.mode = GameMode::Control;
    start(m, r);
    auto geometry = m.world().arena().terrainState();
    kill(m);
    auto dead = m.snapshot();
    expect(m.phase() == MatchPhase::Round && dead.mode.respawn[1].choosing,
           "Death opens only the victim's choice while match continues");
    expect(m.chooseUpgrade(2, UpgradeKind::ActiveQ, dead.round, dead.selectionRevision,
                           dead.mode.respawn[1].life),
           "Victim can roll active");
    float elapsed = dead.mode.elapsed;
    tick(m, 900);
    auto alive = m.snapshot();
    expect(alive.players[1].alive() && alive.players[1].lifeRevision == 1 && alive.mode.elapsed > elapsed + 3,
           "Respawn increments life while world clock keeps running");
    actor(m, 1).actives[0].cooldown = 5;
    kill(m);
    dead = m.snapshot();
    expect(m.chooseUpgrade(2, UpgradeKind::Continue, dead.round, dead.selectionRevision,
                           dead.mode.respawn[1].life),
           "Victim can keep build");
    tick(m, 365);
    alive = m.snapshot();
    expect(alive.players[1].actives[0].cooldown > 1.8f && alive.players[1].actives[0].cooldown < 2.1f,
           "Death does not reset existing active cooldown");
    expect(m.world().arena().terrainState().health == geometry.health, "Respawn preserves terrain");
    expect(!m.action(2, m.round(), 100, false, true, {-1, 0}, false, 0, 0), "Old-life action rejected");
    PlayerInput stale{1};
    stale.lifeRevision = 0;
    m.setInput(2, stale);
    auto x = actor(m, 1).position.x;
    tick(m, 1);
    expect(actor(m, 1).position.x == x, "Old-life movement rejected");
    auto wire = network::encodeSnapshot(m.snapshot());
    expect(network::decodeSnapshot(wire).has_value(), "Control snapshot roundtrip includes respawn state");
    // Stable floor anchor is used for deterministic capture without moving under gravity.
    auto at = m.world().arena().controlAnchors()[m.snapshot().mode.zone];
    actor(m, 0).position = {at.x - 24, at.y - 24};
    actor(m, 0).spawnProtection = 0;
    actor(m, 1).position = {800, 392};
    tick(m, 120);
    expect(m.snapshot().mode.control[0] > .9f, "Solo occupation awards real time");
    auto points = m.snapshot().mode.control[0];
    actor(m, 1).position = {at.x + 24, at.y - 24};
    actor(m, 1).spawnProtection = 0;
    tick(m, 30);
    expect(m.snapshot().mode.control[0] < points + .05f, "Both players contest without points");
    Match timeout(false, 4);
    start(timeout, r);
    for (int n = 0; n < 480 * 120 && timeout.phase() == MatchPhase::Round; ++n)
        timeout.update(dt);
    expect(timeout.phase() == MatchPhase::Finished && timeout.snapshot().winner == 0,
           "Eight-minute empty control ends as draw");
}

int main()
{
    try
    {
        race();
        chaos();
        control();
        std::cout << "Race, chaos and continuous control passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
