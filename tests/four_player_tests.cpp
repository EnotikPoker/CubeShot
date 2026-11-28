#include "game/BotMatch.h"
#include "game/Collision.h"
#include "game/Prediction.h"
#include "network/Protocol.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

namespace
{
void check(bool c, const char* text)
{
    if (!c)
        throw std::runtime_error(text);
}

constexpr float dt = 1.f / 120;

void advance(Match& m, unsigned n)
{
    while (n--)
        m.update(dt);
}

Match make(MatchFormat format, GameMode mode = GameMode::Duel, unsigned count = 4)
{
    Match m(false, 42);
    m.connect(1);
    MatchRules r;
    r.format = format;
    r.participants = count;
    r.mode = mode;
    check(m.selectRules(1, r, m.snapshot().selectionRevision), "Configure match rules");
    for (unsigned id = 2; id <= count; ++id)
        m.connect(id);
    for (unsigned id = 1; id <= count; ++id)
        m.setReady(id, true);
    check(m.phase() == MatchPhase::Countdown, "All occupied slots are required to start");
    advance(m, 360);
    return m;
}

void formats()
{
    for (auto mode : {GameMode::Duel, GameMode::ArmsRace, GameMode::Chaos})
    {
        auto f = make(MatchFormat::FreeForAll, mode);
        auto& p = const_cast<std::vector<Player>&>(f.world().players());
        p[1].health = 0;
        f.update(dt);
        check(f.phase() == MatchPhase::Round, "One elimination does not finish FFA");
        p[2].health = p[3].health = 0;
        f.update(dt);
        auto s = f.snapshot();
        check(s.winner == 1 && s.winnerTeam == 0 && s.score[0] == 1, "Last living FFA player scores");
        advance(f, 480);
        check(f.phase() == MatchPhase::Armory, "Four-second aftermath opens choices");
        advance(f, 3601);
        check(f.phase() == MatchPhase::Countdown, "Unanswered choices continue after 30 seconds");
        if (mode == GameMode::ArmsRace)
            check(f.snapshot().loadout[0] == armsChain(s.rules.stages)[1],
                  "FFA arms progression follows round wins");
        auto t = make(MatchFormat::Teams, mode);
        auto& bodies = const_cast<std::vector<Player>&>(t.world().players());
        bodies[1].health = 0;
        t.update(dt);
        check(t.phase() == MatchPhase::Round, "One teammate survives team elimination");
        bodies[3].health = 0;
        t.update(dt);
        s = t.snapshot();
        check(s.winnerTeam == 1 && s.winner == 0 && s.teamScore[0] == 1 && s.score[0] == 0,
              "Team scores separately from players");
        advance(t, 480);
        for (unsigned id = 1; id <= 4; ++id)
            t.chooseWeapon(id, false, t.round(), t.snapshot().selectionRevision);
        t.update(dt);
        if (mode == GameMode::ArmsRace)
            check(t.snapshot().loadout[0] == t.snapshot().loadout[2] &&
                      t.snapshot().loadout[0] == armsChain(s.rules.stages)[1],
                  "Both teammates receive the shared stage weapon");
    }
    auto f = make(MatchFormat::FreeForAll);
    for (auto& p : const_cast<std::vector<Player>&>(f.world().players()))
        p.health = 0;
    f.update(dt);
    check(!f.snapshot().winner && !f.snapshot().winnerTeam && f.snapshot().score[0] == 0,
          "Simultaneous elimination is a draw");
}

void choicesAndDisconnect()
{
    auto f = make(MatchFormat::FreeForAll);
    auto& p = const_cast<std::vector<Player>&>(f.world().players());
    auto before = p[2];
    f.disconnect(3);
    check(f.snapshot().slots[2] == SlotKind::Bot && f.world().players()[2].health == before.health &&
              f.world().players()[2].position.x == before.position.x,
          "Replacement preserves body and match");
    f.connect(3);
    check(f.snapshot().slots[2] == SlotKind::Bot, "No joining an active match");
    p[1].health = p[2].health = p[3].health = 0;
    f.update(dt);
    advance(f, 480 + 120 * 29);
    check(f.chooseUpgrade(1, UpgradeKind::Ability, f.round(), f.snapshot().selectionRevision),
          "Late choice accepted before deadline");
    advance(f, 241);
    check(f.phase() == MatchPhase::Armory, "Started wheel survives decision deadline");
    advance(f, 720);
    check(f.phase() == MatchPhase::Countdown || f.phase() == MatchPhase::Round,
          "Full result review ends before next round");
}

void control()
{
    auto m = make(MatchFormat::Teams, GameMode::Control);
    auto& p = const_cast<std::vector<Player>&>(m.world().players());
    auto at = m.world().arena().controlAnchors()[0];
    p[0].position = {at.x - 24, at.y - 24};
    p[2].position = p[0].position;
    for (unsigned tick = 0; tick < 120; ++tick)
    {
        p[0].position = {at.x - 24, at.y - 24};
        p[2].position = {at.x - 24, at.y - 24};
        p[0].velocity = p[2].velocity = {};
        m.update(dt);
    }
    check(m.snapshot().mode.control[0] > .9 && m.snapshot().mode.control[0] < 1.1,
          "Two teammates capture at normal speed");
    double before = m.snapshot().mode.control[0];
    p[1].position = {at.x - 24, at.y - 24};
    p[1].velocity = {};
    m.update(dt);
    check(m.snapshot().mode.control[0] == before, "Opposing team contests zone");
}

void wireAndPrediction()
{
    for (auto format : {MatchFormat::FreeForAll, MatchFormat::Teams})
    {
        auto m = make(format);
        auto s = m.snapshot();
        auto encoded = network::encodeSnapshot(s);
        auto decoded = network::decodeSnapshot(encoded);
        check(decoded && decoded->players.size() == 4 && decoded->rules.format == format,
              "Four-participant snapshot round trip");
        check(encoded.ByteSizeLong() < network::maxPacketBytes, "Snapshot fits packet limit");
        Prediction pred;
        pred.reconcile(*decoded, 4);
        check(pred.player().has_value(), "Fourth participant can predict its own body");
        encoded.mutable_players(3)->set_id(2);
        check(!network::decodeSnapshot(encoded), "Duplicate participant IDs rejected");
        encoded = network::encodeSnapshot(s);
        encoded.set_participants(5);
        check(!network::decodeSnapshot(encoded), "Invalid capacity rejected");
    }
}

void friendlyFire()
{
    Arena arena{{{0, 0}, {960, 540}},
                {{{0, 440}, {960, 100}}},
                {{100, 392}, {155, 392}, {400, 392}, {800, 392}},
                MapId::Test};
    for (unsigned k = 0; k < weaponCount; ++k)
    {
        World world(arena, 4);
        world.setTeams({{1, 1, 2, 2}}, false);
        world.equip(1, WeaponKind(k));
        for (unsigned n = 0; n < 480; ++n)
        {
            world.requestAction(1, false, true, {1, 0});
            world.setPlayerInput(1, {0, false, true, {1, 0}, false, true});
            world.update(dt);
            world.takeEvents();
        }
        const auto& ally = world.players()[1];
        check(ally.health == 100 && ally.burning == 0 && ally.bleeding == 0,
              "All weapons preserve allied health and statuses with friendly fire disabled");
    }
    World world(arena, 4);
    world.setTeams({{1, 1, 2, 2}}, true);
    world.requestAction(1, false, true, {1, 0});
    for (unsigned n = 0; n < 40; ++n)
        world.update(dt);
    check(world.players()[1].health < 100, "Enabled friendly fire damages allies");
    world = World(arena, 4);
    world.setTeams({{1, 1, 2, 2}}, false);
    world.requestAction(1, false, true, {1, 0});
    for (unsigned n = 0; n < 80; ++n)
        world.update(dt);
    check(world.players()[1].health == 100 && world.players()[2].health == 80,
          "Friendly body does not absorb a projectile");
}

void spawns()
{
    for (auto info : mapCatalog())
        if (validMap(info.id))
        {
            auto arena = Arena::create(info.id);
            auto places = arena.combatSpawns();
            check(places.size() == 4, "Four map spawns");
            for (unsigned i = 0; i < 4; ++i)
            {
                Rect b{places[i], {48, 48}};
                for (auto wall : arena.platforms)
                    check(!collision::overlaps(b, wall), "Spawn outside solids");
                for (unsigned j = 0; j < i; ++j)
                    check(!collision::overlaps(b, {places[j], {48, 48}}), "Separate spawn bodies");
                check(collision::isSupported(b, arena.bounds, arena.platforms), "Supported starting body");
            }
            arena.damageTerrain({arena.bounds.size.x / 2, arena.bounds.size.y / 2}, 10000, 10000, false,
                                true);
            check(arena.combatSpawns().size() == 4, "Spawn search survives destroyed cover");
        }
}
} // namespace

int main()
{
    try
    {
        formats();
        choicesAndDisconnect();
        control();
        wireAndPrediction();
        friendlyFire();
        spawns();
        std::cout << "Four-player rules, teams, choices, protocol and maps passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
