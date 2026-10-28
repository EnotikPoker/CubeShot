#include "game/Match.h"
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace cubeshot;

namespace
{
constexpr float dt = 1.f / 120;

void expect(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

void advance(Match& m, int ticks)
{
    for (int i = 0; i < ticks; ++i)
        m.update(dt);
}

void start(Match& m)
{
    m.connect(1);
    m.connect(2);
    m.setReady(1, true);
    m.setReady(2, true);
    advance(m, 360);
}

void win(Match& m)
{
    const bool swap = m.world().players()[0].position.x > m.world().players()[1].position.x;
    m.setInput(1, {swap ? -1.f : 1.f});
    m.setInput(2, {swap ? 1.f : -1.f});
    advance(m, 120);
    m.setInput(1, {});
    m.setInput(2, {});
    for (unsigned shot = 1; shot <= 5; ++shot)
    {
        const auto from = m.world().players()[0].weaponOrigin(), to = m.world().players()[1].weaponOrigin();
        expect(m.action(1, m.round(), shot, false, true, {to.x - from.x, to.y - from.y}),
               "Shot accepted in live round");
        for (int tick = 0; tick < 60 && m.phase() == MatchPhase::Round; ++tick)
            m.update(dt);
    }
    expect(m.phase() == MatchPhase::RoundEnd, "A kill enters aftermath, including the deciding round");
}

void armory()
{
    Match m(false, 42);
    start(m);
    win(m);
    const auto death = m.snapshot();
    expect(std::abs(death.remaining - 4) < .01f && death.score[0] == 1 && death.corpses.size() == 1,
           "Kill gives one point and four full seconds with a physical corpse");
    expect(!m.action(2, m.round(), 9, false, true, {1, 0}), "Dead player cannot fire after losing");
    const auto x = m.world().players()[0].position.x;
    m.setInput(1, {1});
    advance(m, 12);
    m.setInput(1, {});
    expect(m.world().players()[0].position.x > x + 10, "Survivor can walk during aftermath");
    expect(m.action(1, m.round(), 10, false, true, {1, 0}), "Survivor can shoot during aftermath");
    const auto score = m.snapshot().score;
    advance(m, 360);
    expect(m.phase() == MatchPhase::RoundEnd && m.snapshot().score == score,
           "Aftermath does not end early or change the awarded result");
    advance(m, 120);
    auto state = m.snapshot();
    expect(state.phase == MatchPhase::Armory && !state.corpses.empty(),
           "Weapon choice follows aftermath with remains still visible");
    expect(!m.chooseWeapon(1, true, state.round - 1, state.selectionRevision),
           "Old-round weapon choice is rejected");
    expect(!m.chooseWeapon(1, true, state.round, state.selectionRevision - 1),
           "Old-revision weapon choice is rejected");
    expect(m.chooseWeapon(1, true, state.round, state.selectionRevision), "One roulette draw accepted");
    const auto rolled = m.snapshot().rolled[0];
    expect(!m.chooseWeapon(1, true, state.round, state.selectionRevision) &&
               !m.chooseWeapon(1, false, state.round, state.selectionRevision),
           "A draw cannot be repeated or changed to keep after outcome is known");
    advance(m, 60);
    expect(m.phase() == MatchPhase::Armory && m.snapshot().weaponChoice[0] == WeaponChoice::Spinning,
           "Roulette has real animation time");
    expect(m.chooseWeapon(2, false, state.round, state.selectionRevision),
           "Other player independently keeps pistol");
    advance(m, 240);
    expect(m.phase() == MatchPhase::Armory, "Result remains visible before countdown begins");
    advance(m, 90);
    state = m.snapshot();
    expect(state.phase == MatchPhase::Countdown && state.round == death.round + 1 &&
               state.loadout[0] == rolled && state.players[0].weapon.kind == rolled &&
               state.players[1].weapon.kind == WeaponKind::Pistol,
           "Both completed choices preserve or apply weapon in next round");
    expect(state.corpses.empty() && state.pieces.empty() && state.players[1].health == 100 &&
               state.players[0].position.x > state.players[1].position.x,
           "Next round swaps sides and resets remains and HP");
    expect(!m.chooseWeapon(2, true, state.round, state.selectionRevision),
           "Roulette cannot run outside intermission");
    m.disconnect(2);
    expect(m.snapshot().phase == MatchPhase::Lobby && m.snapshot().loadout[0] == WeaponKind::Pistol &&
               m.snapshot().pieces.empty(),
           "Disconnect clears weapon selection and physical remains");
    std::set<WeaponKind> outcomes;
    for (unsigned seed = 1; seed <= 80; ++seed)
    {
        Match sample(false, seed);
        start(sample);
        win(sample);
        advance(sample, 480);
        const auto s = sample.snapshot();
        sample.chooseWeapon(1, true, s.round, s.selectionRevision);
        outcomes.insert(sample.snapshot().rolled[0]);
    }
    expect(outcomes.size() == 7 && outcomes.count(WeaponKind::Pistol),
           "All seven weapons, including current pistol, can occur");
}
} // namespace

int main()
{
    try
    {
        armory();
        std::cout << "Aftermath and weapon roulette passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
