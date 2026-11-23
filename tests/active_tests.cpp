#include "game/AttackStats.h"
#include "game/BotController.h"
#include "game/World.h"
#include "network/Protocol.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;
constexpr float dt = 1.f / 120;

void expect(bool b, const char* message)
{
    if (!b)
        throw std::runtime_error(message);
}

Arena flat(float other = 350)
{
    return {{{0, 0}, {1200, 540}}, {{{0, 440}, {1200, 100}}}, {{100, 392}, {other, 392}}, MapId::Duel};
}

void advance(World& w, unsigned ticks)
{
    for (unsigned i = 0; i < ticks; ++i)
        w.update(dt);
}

void blink()
{
    auto arena = flat(800);
    arena.platforms.push_back({{200, 250}, {40, 190}});
    World w(arena, 2);
    w.setActives(1, {ActiveKind::Blink, ActiveKind::Dash});
    w.requestAction(1, false, false, {1, 0}, 1, false, 1);
    w.update(dt);
    expect(std::abs(w.players()[0].position.x - 320) < .01f,
           "Teleport crosses a wall to its safe far-side destination");
    expect(w.players()[0].actives[0].cooldown > 7.9f && w.players()[0].teleportRevision == 1,
           "Cast consumes one cooldown and records discontinuity");
    for (int i = 0; i < 1200; ++i)
    {
        PlayerInput held;
        held.activeQ = true;
        w.setPlayerInput(1, held);
        w.update(dt);
    }
    expect(w.players()[0].teleportRevision == 1, "Holding a key does not recast after cooldown");
    Player p;
    p.position = {0, 0};
    p.actives[0].kind = ActiveKind::Blink;
    p.aimDirection = {-1, 0};
    expect(!p.activate(0, arena) && p.actives[0].cooldown == 0, "Blocked cast does not consume cooldown");
}

void dash()
{
    auto arena = flat(900);
    World w(arena, 2);
    w.setActives(1, {ActiveKind::Dash, ActiveKind::None});
    w.requestAction(1, false, false, {1, 0}, 1, false, 1);
    advance(w, 40);
    expect(std::abs(w.players()[0].position.x - 300) < .2f,
           "Dash travels 200 then stops without held movement");
    arena.platforms.push_back({{200, 250}, {20, 190}});
    World blocked(arena, 2);
    blocked.setActives(1, {ActiveKind::Dash, ActiveKind::None});
    blocked.requestAction(1, false, false, {1, 0}, 1, false, 1);
    advance(blocked, 40);
    expect(blocked.players()[0].position.x <= 152.001f, "Dash cannot tunnel through a wall");
}

void simultaneous()
{
    World w(flat(900), 2);
    w.setActives(1, {ActiveKind::Dash, ActiveKind::Barrier});
    w.requestAction(1, true, true, {1, 0}, 7, false, 3);
    w.update(dt);
    auto p = w.players()[0];
    expect(p.activeSerial == 2 && p.dashTime > 0 && p.barrier == 60 && p.weapon.ammo == 11,
           "Both active slots can cast alongside jump and shooting in one simulation step");
    expect(p.actives[0].cooldown > 5 && p.actives[1].cooldown > 11, "Slots have independent cooldowns");
}

void defenses()
{
    auto arena = flat();
    World w(arena, 2);
    Abilities armor;
    armor.add(AbilityKind::Armor);
    w.setAbilities(2, armor);
    w.setActives(2, {ActiveKind::Barrier, ActiveKind::Heal});
    w.requestAction(2, false, false, {-1, 0}, 1, false, 1);
    w.update(dt);
    for (int shot = 0; shot < 3; ++shot)
    {
        w.requestAction(1, false, true, {1, 0});
        advance(w, 48);
    }
    expect(w.players()[1].health == 100 && w.players()[1].shields == 0 &&
               std::abs(w.players()[1].barrier - 40) < .01f,
           "Armor absorbs first two attacks, then barrier absorbs health damage");
    Player p;
    p.health = 50;
    p.burning = 2;
    p.bleeding = 3;
    p.actives[0].kind = ActiveKind::Heal;
    expect(p.activate(0, arena) && p.health == 80 && p.burning == 0 && p.bleeding == 0,
           "Heal restores 30 and clears both damage-over-time effects");
    p.health = 100;
    p.actives[0].cooldown = 0;
    expect(!p.activate(0, arena) && p.actives[0].cooldown == 0, "Full healthy player cannot waste healing");
}

void attacks()
{
    for (auto kind : {ActiveKind::Harpoon, ActiveKind::Wave, ActiveKind::Slam})
    {
        auto arena = flat(kind == ActiveKind::Harpoon ? 500 : 190);
        World w(arena, 2);
        w.setActives(1, {kind, ActiveKind::None});
        w.requestAction(1, false, false, {1, 0}, 1, false, 1);
        advance(w, 90);
        expect(w.players()[1].health < 100, "Offensive active deals actual health damage");
        expect(w.players()[0].weapon.ammo == 12, "Active attack does not consume weapon ammunition");
    }
    auto arena = flat(900);
    World bomb(arena, 2);
    bomb.setActives(1, {ActiveKind::Bomb, ActiveKind::None});
    bomb.requestAction(1, false, false, {0, 1}, 1, false, 1);
    advance(bomb, 180);
    expect(bomb.players()[0].health < 100, "Own bomb can hurt its owner");
}

void deferredSlam()
{
    auto a = flat(220);
    a.spawnPoints[0] = {100, 180};
    World w(a, 2);
    w.setActives(1, {ActiveKind::Slam, ActiveKind::Barrier});
    w.requestAction(1, false, false, {1, 0}, 91, false, 1);
    w.update(dt);
    w.takeEvents();
    w.requestAction(1, false, false, {1, 0}, 92, false, 2);
    w.update(dt);
    w.takeEvents();
    bool landed = false;
    for (int n = 0; n < 90; ++n)
    {
        w.update(dt);
        for (const auto& e : w.takeEvents())
            if (e.kind == EventKind::ActiveCast && e.active == ActiveKind::Slam && e.radius > 0)
            {
                expect(e.shotSequence == 91 && e.volley == (0x80000000u | 1u),
                       "Landing retains the original slam action even after another active cast");
                landed = true;
            }
    }
    expect(landed, "Airborne slam produces a landing event");
    World far(flat(650), 2);
    far.setActives(1, {ActiveKind::Harpoon, ActiveKind::None});
    far.requestAction(1, false, false, {1, 0}, 1, false, 1);
    advance(far, 80);
    expect(far.players()[1].health == 90, "Harpoon retains all ten damage across its full range");
}

void wireState()
{
    Match m;
    m.connect(1);
    m.connect(2);
    auto s = m.snapshot();
    s.activeLoadout[0] = {ActiveKind::Blink, ActiveKind::Heal};
    s.players[0].setActives(s.activeLoadout[0]);
    s.players[0].actives[0].cooldown = 3;
    s.players[0].barrier = 40;
    s.players[0].barrierTime = 1;
    auto encoded = network::encodeSnapshot(s);
    auto copy = network::decodeSnapshot(encoded);
    expect(copy && copy->players[0].actives[0].cooldown == 3 &&
               copy->activeLoadout[0][1] == ActiveKind::Heal && copy->players[0].barrier == 40,
           "Active loadouts and transient state survive wire roundtrip");
    auto bad = encoded;
    bad.mutable_players(0)->mutable_active_slots(0)->set_cooldown(-1);
    expect(!network::decodeSnapshot(bad), "Reject invalid active timers");
    bad = encoded;
    bad.set_active_loadout(1, unsigned(ActiveKind::Blink));
    expect(!network::decodeSnapshot(bad), "Reject duplicate active loadout");
}

void rouletteAndDuplicates()
{
    Match m(false, 42);
    m.connect(1);
    m.connect(2);
    m.setReady(1, true);
    m.setReady(2, true);
    m.update(3);
    BotController bot(m.world().arena(), BotDifficulty::Hard, 42);
    bool held = false;
    std::uint64_t seq = 0;
    for (int i = 0; i < 8000 && m.phase() == MatchPhase::Round; ++i)
    {
        auto input = bot.update(observeBot(m.world(), 1, 2), dt);
        m.setInput(1, input);
        if (input.jump || (input.shoot && !held))
            m.action(1, m.round(), ++seq, input.jump, input.shoot && !held, input.aimDirection);
        held = input.shoot;
        m.update(dt);
        m.takeEvents();
    }
    expect(m.phase() == MatchPhase::RoundEnd, "Roulette fixture ends a real pistol round");
    for (int i = 0; i < 600 && m.phase() == MatchPhase::RoundEnd; ++i)
        m.update(dt);
    auto state = m.snapshot();
    expect(m.chooseUpgrade(1, UpgradeKind::ActiveQ, state.round, state.selectionRevision),
           "Q uses the shared intermission choice");
    auto rolled = m.snapshot().rolledActive[0];
    expect(!m.chooseUpgrade(1, UpgradeKind::ActiveE, state.round, state.selectionRevision) &&
               !m.chooseWeapon(1, true, state.round, state.selectionRevision),
           "One active roll excludes every other category");
    m.chooseWeapon(2, false, state.round, state.selectionRevision);
    for (int i = 0; i < 289; ++i)
        m.update(dt);
    expect(m.snapshot().weaponChoice[0] == WeaponChoice::Revealing &&
               m.snapshot().activeLoadout[0][0] == rolled && m.snapshot().spinRemaining[0] > 4.98f,
           "Active result applies once then remains readable five seconds");
    for (int i = 0; i < 480; ++i)
        m.update(dt);
    expect(m.phase() == MatchPhase::Armory, "Four seconds do not skip the result");
    for (int i = 0; i < 200 && m.phase() == MatchPhase::Armory; ++i)
        m.update(dt);
    expect(m.phase() == MatchPhase::Countdown && m.snapshot().players[0].actives[0].kind == rolled,
           "Next round equips the authoritative active");
    m.update(3);
    auto round = m.round();
    expect(m.action(1, round, 100000, false, false, {1, 0}, false, 1), "Reliable active action accepted");
    expect(!m.action(1, round, 100000, false, false, {1, 0}, false, 1) &&
               !m.action(1, round - 1, 100001, false, false, {1, 0}, false, 1),
           "Duplicate and old-round casts rejected");
    m.update(dt);
    expect(m.world().players()[0].activeSerial <= 1, "Duplicate cannot execute another cast");
}

int main()
{
    try
    {
        blink();
        dash();
        simultaneous();
        defenses();
        attacks();
        deferredSlam();
        wireState();
        rouletteAndDuplicates();
        std::cout << "Active abilities, collisions, defenses and wire passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
