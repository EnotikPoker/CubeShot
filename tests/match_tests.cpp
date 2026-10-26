#include "game/Match.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

namespace
{
void expect(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

void advance(Match& m, unsigned ticks)
{
    for (unsigned i = 0; i < ticks; ++i)
        m.update(1.0f / 120);
}

void nextRound(Match& m)
{
    for (int tick = 0; tick < 600 && m.phase() == MatchPhase::RoundEnd; ++tick)
        m.update(1.f / 120);
    expect(m.phase() == MatchPhase::Armory, "Aftermath opens weapon choice");
    const auto state = m.snapshot();
    expect(m.chooseWeapon(1, false, state.round, state.selectionRevision) &&
               m.chooseWeapon(2, false, state.round, state.selectionRevision),
           "Both players keep their weapon");
    m.update(1.f / 120);
    advance(m, 360);
    expect(m.phase() == MatchPhase::Round, "Both choices start the next countdown and round");
}

void ready(Match& m)
{
    m.connect(1);
    m.connect(2);
    m.setReady(1, true);
    m.setReady(2, true);
    advance(m, 360);
}

void approach(Match& m)
{
    const bool swapped = m.world().players()[0].position.x > m.world().players()[1].position.x;
    m.setInput(1, {swapped ? -1.f : 1.f});
    m.setInput(2, {swapped ? 1.f : -1.f});
    advance(m, 120);
    m.setInput(1, {});
    m.setInput(2, {});
}

Vec2 aim(const Match& m, PlayerId id)
{
    const auto from = m.world().players()[id - 1].weaponOrigin();
    const auto to = m.world().players()[2 - id].weaponOrigin();
    return {to.x - from.x, to.y - from.y};
}

void win(Match& m, PlayerId id)
{
    approach(m);
    for (unsigned i = 1; i <= 5; ++i)
    {
        expect(m.action(id, m.round(), i, false, true, aim(m, id)), "Accept fresh shot");
        advance(m, 60);
    }
}

void rules()
{
    Match m;
    const auto& a = m.world().arena();
    expect(a.platforms.size() == 3 && a.spawnPoints.size() == 2 && a.spawnPoints[0].x == 80 &&
               a.spawnPoints[1].x == 832,
           "Symmetric arena has exactly two spawns and two platforms");
    m.connect(1);
    m.setReady(1, true);
    advance(m, 400);
    expect(m.phase() == MatchPhase::Lobby, "One player cannot start");
    m.connect(2);
    m.setReady(2, true);
    expect(m.phase() == MatchPhase::Countdown, "Both ready start countdown");
    advance(m, 120);
    m.disconnect(2);
    expect(m.phase() == MatchPhase::Lobby && m.snapshot().remaining == 0 && !m.snapshot().ready[0],
           "Disconnect cancels countdown");
    ready(m);
    expect(m.phase() == MatchPhase::Round, "Countdown lasts three seconds");
    const auto firstRound = m.round();
    const auto firstSpawn = m.world().players()[0].position.x;
    win(m, 1);
    expect(m.phase() == MatchPhase::RoundEnd && m.snapshot().score[0] == 1, "Death awards one point");
    expect(m.world().players()[1].health == 0, "Five pistol hits kill");
    expect(m.action(1, m.round(), 6, false, true, {1, 0}), "Winner can shoot remains during aftermath");
    nextRound(m);
    expect(m.phase() == MatchPhase::Round && m.round() != firstRound, "Next round after both weapon choices");
    expect(m.world().players()[0].position.x != firstSpawn && m.world().players()[1].health == 100 &&
               m.world().bullets().empty(),
           "Swap spawns and reset health/bullets");
    expect(!m.action(1, firstRound, 100, false, true, {-1, 0}), "Reject actions from past round");
    for (int i = 0; i < 4; ++i)
    {
        win(m, 1);
        if (i < 3)
            nextRound(m);
        else
            advance(m, 480);
    }
    expect(m.phase() == MatchPhase::Finished && m.snapshot().score[0] == 5 && m.snapshot().winner == 1,
           "First to five wins match");
    m.setReady(1, true);
    advance(m, 600);
    expect(m.phase() == MatchPhase::Finished, "Rematch requires both votes");
    m.setReady(2, true);
    expect(m.phase() == MatchPhase::Countdown && m.snapshot().score[0] == 0, "Rematch resets score");
    advance(m, 360);
    m.disconnect(1, AbortReason::PeerTimeout);
    expect(m.phase() == MatchPhase::Lobby && m.snapshot().score[0] == 0 && m.snapshot().score[1] == 0 &&
               m.snapshot().aborted == AbortReason::PeerTimeout,
           "Disconnect aborts without winner");
}

void mapChangeAfterMatch()
{
    for (bool voting : {false, true})
    {
        Match m(voting);
        m.connect(1);
        m.connect(2);
        if (voting)
        {
            m.selectMap(1, MapId::Duel, 0);
            m.selectMap(2, MapId::Duel, m.snapshot().selectionRevision);
        }
        m.setReady(1, true);
        m.setReady(2, true);
        advance(m, 360);
        for (int i = 0; i < 5; ++i)
        {
            win(m, 1);
            if (i < 4)
                nextRound(m);
            else
                advance(m, 480);
        }
        const auto map = m.snapshot().map;
        m.setReady(1, true);
        m.setReady(2, true);
        expect(m.snapshot().map == map, "Rematch repeats the selected map without another random choice");
        advance(m, 360);
        for (int i = 0; i < 5; ++i)
        {
            win(m, 1);
            if (i < 4)
                nextRound(m);
            else
                advance(m, 480);
        }
        const auto revision = m.snapshot().selectionRevision;
        expect(!m.changeMap(1, revision - 1), "Stale change-map request cannot cancel result");
        if (!voting)
            expect(!m.changeMap(2, revision), "LAN guest cannot request map change");
        expect(m.changeMap(voting ? 2 : 1, revision),
               "Online either player or LAN host can reopen selection");
        expect(m.phase() == MatchPhase::Lobby && !m.snapshot().ready[0] && !m.snapshot().ready[1] &&
                   m.snapshot().score[0] == 0,
               "Map-change screen clears readiness and score");
        if (voting)
        {
            expect(m.snapshot().votes[0] == -1 && m.snapshot().votes[1] == -1,
                   "New selection clears previous votes");
            m.setReady(1, true);
            m.setReady(2, true);
            expect(m.phase() == MatchPhase::Lobby, "New map selection again requires votes");
        }
        expect(m.selectMap(1, MapId::NatureLarge, m.snapshot().selectionRevision),
               "A different map can be selected for the next match");
    }
}

void actionsAndDraw()
{
    Match m;
    ready(m);
    approach(m);
    for (unsigned i = 1; i <= 5; ++i)
    {
        expect(m.action(1, m.round(), i, false, true, aim(m, 1)), "Player one shot");
        expect(!m.action(1, m.round(), i, false, true, aim(m, 1)), "Duplicate action cannot repeat");
        expect(m.action(2, m.round(), i, false, true, aim(m, 2)), "Player two shot");
        advance(m, 60);
    }
    expect(m.phase() == MatchPhase::RoundEnd && m.snapshot().winner == 0 && m.snapshot().score[0] == 0 &&
               m.snapshot().score[1] == 0,
           "Mutual death in same tick is draw");
    Match captured;
    ready(captured);
    approach(captured);
    captured.action(1, captured.round(), 1, false, true, {1, 0});
    captured.setInput(1, {0, false, false, {-1, 0}});
    advance(captured, 1);
    expect(captured.world().bullets().size() == 1 && captured.world().bullets()[0].velocity.x > 0,
           "Shot uses aim at press, even if control arrives afterward");
    const auto oldId = captured.world().bullets()[0].id;
    captured.disconnect(2);
    ready(captured);
    approach(captured);
    captured.action(1, captured.round(), 1, false, true, {1, 0});
    advance(captured, 1);
    expect(captured.world().bullets()[0].id > oldId, "Bullet IDs survive round resets");
}
} // namespace

int main()
{
    try
    {
        rules();
        actionsAndDraw();
        mapChangeAfterMatch();
        std::cout << "Match rules passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
