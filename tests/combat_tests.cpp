#include "game/BodyPhysics.h"
#include "game/Collision.h"
#include "game/Match.h"
#include "game/Prediction.h"
#include "network/Protocol.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

namespace
{
void expect(bool value, const char* text)
{
    if (!value)
        throw std::runtime_error(text);
}

bool near(float a, float b)
{
    return std::abs(a - b) < .02f;
}

Arena flat()
{
    return {{{0, 0}, {960, 540}}, {{{0, 440}, {960, 100}}}, {{100, 392}, {200, 392}}, MapId::Duel};
}

Player body(PlayerId id, Vec2 at)
{
    Player p;
    p.id = id;
    p.position = at;
    return p;
}

void bodies()
{
    const auto a = flat();
    std::vector<Player> p{body(1, {100, 392}), body(2, {180, 392})};
    for (int i = 0; i < 120; ++i)
    {
        p[0].applyInput({1});
        p[1].applyInput({-1});
        simulateBodies(p, a, 1.f / 120);
        expect(!collision::overlaps(p[0].bounds(), p[1].bounds()),
               "Head-on bodies cannot intersect or cross");
    }
    expect(p[0].position.x < p[1].position.x, "Head-on order preserved");
    p = {body(1, {100, 392}), body(2, {148, 392})};
    for (int i = 0; i < 120; ++i)
    {
        p[0].applyInput({1});
        p[1].applyInput({});
        simulateBodies(p, a, 1.f / 120);
    }
    expect(p[1].position.x > 250 && near(p[1].position.x - p[0].position.x, 48),
           "Walking pushes idle player");
    p = {body(1, {864, 392}), body(2, {912, 392})};
    for (int i = 0; i < 120; ++i)
    {
        p[0].applyInput({1});
        p[1].applyInput({});
        simulateBodies(p, a, 1.f / 120);
    }
    expect(near(p[0].position.x, 864) && near(p[1].position.x, 912),
           "Wall blocks pushing without penetration");
    p = {body(1, {200, 200}), body(2, {200, 392})};
    for (int i = 0; i < 120; ++i)
        simulateBodies(p, a, 1.f / 120);
    expect(near(p[0].position.y, 344) && p[0].grounded, "Player can land on another body");
    for (int i = 0; i < 120; ++i)
        simulateBodies(p, a, 1.f / 120);
    expect(near(p[0].position.y, 344) && near(p[1].position.y, 392), "Stack is stable without jitter");
    p[0].applyInput({0, true});
    simulateBodies(p, a, 1.f / 120);
    expect(p[0].velocity.y < 0 && !p[0].grounded, "Jump from player's head");
    p = {body(1, {200, 344}), body(2, {200, 392})};
    p[1].takeDamage(100);
    for (int i = 0; i < 120; ++i)
        simulateBodies(p, a, 1.f / 120);
    expect(near(p[0].position.y, 392), "Dead support no longer blocks living body");
    auto wall = a;
    wall.platforms.push_back({{300, 100}, {20, 340}});
    p = {body(1, {204, 392}), body(2, {252, 392})};
    for (int i = 0; i < 120; ++i)
    {
        p[0].applyInput({1});
        simulateBodies(p, wall, 1.f / 120);
    }
    expect(near(p[1].position.x, 252) && near(p[0].position.x, 204),
           "Static wall constrains both pushed bodies");
    // Contact uses the same model during prediction, and never assigns damage to the remote body.
    WorldSnapshot s;
    s.phase = MatchPhase::Round;
    s.round = 1;
    s.players = {body(1, {400, 392}), body(2, {448, 392})};
    Prediction prediction;
    prediction.reconcile(s, 1);
    prediction.advance({1});
    expect(prediction.player()->position.x > 400 &&
               prediction.player()->position.x < 400 + Player::moveSpeed / 120,
           "Prediction includes body contact");
    s.map = MapId::NatureLarge;
    s.round++;
    s.players = {body(1, {1000, 752}), body(2, {1516, 752})};
    prediction.reconcile(s, 1);
    expect(prediction.action(99, false, true, {1, 0}).has_value(),
           "Shot immediately creates presentation event on large map");
    prediction.advance({});
    expect(!prediction.bullets().empty() && prediction.bullets()[0].position.x > 960,
           "Large-map predicted bullet is not clipped by old screen bounds");
    s.map = MapId::FactorySmall;
    s.round++;
    s.players = {body(1, {36, 392}), body(2, {876, 392})};
    prediction.reconcile(s, 1);
    expect(prediction.bullets().empty() && prediction.commands().empty(),
           "Map change clears prediction history and bullets");
    // Big World updates are internally substepped.
    auto pair = a;
    pair.spawnPoints = {{100, 392}, {200, 392}};
    World w(pair);
    w.setPlayerInput(1, {1});
    w.setPlayerInput(2, {-1});
    w.update(1);
    expect(!collision::overlaps(w.players()[0].bounds(), w.players()[1].bounds()),
           "Large frame cannot tunnel through another player");
}

void maps()
{
    expect(mapCatalog().size() == 11, "Nine new maps and two legacy arenas");
    for (const auto& info : mapCatalog())
    {
        auto a = Arena::create(info.id, true);
        World test(a);
        expect(test.players().size() == 4, "Every test map has a player and three targets");
        for (const auto& p : test.players())
        {
            for (const auto& solid : a.platforms)
                expect(!collision::overlaps(p.bounds(), solid), "Spawn must not intersect terrain");
            for (const auto& other : test.players())
                if (p.id != other.id)
                    expect(!collision::overlaps(p.bounds(), other.bounds()),
                           "Spawns must not intersect each other");
        }
        test.update(2);
        for (std::size_t i = 0; i < 4; ++i)
            expect(near(test.players()[i].position.y, a.spawnPoints[i].y),
                   "Targets remain supported and stationary");
        if (info.id == MapId::Test)
            continue;
        a = Arena::create(info.id);
        World duel(a);
        expect(duel.players().size() == 2, "Duel always contains exactly two players");
        expect(near(a.spawnPoints[0].x + a.spawnPoints[1].x + 48, a.bounds.size.x),
               "Duel spawn positions are symmetric");
        if (info.id == MapId::Duel)
            continue;
        const unsigned size = (static_cast<unsigned>(info.id) - 1) % 3;
        expect(near(a.bounds.size.x, size == 0   ? 960
                                     : size == 1 ? 1280
                                                 : 1600),
               "Map size matches catalog category");
        // Search real jump trajectories from reachable surfaces. This catches ceilings,
        // overlong gaps and inaccessible platforms, not only nominal jump height.
        std::vector<bool> reached(a.platforms.size(), false);
        reached[0] = true;
        for (unsigned pass = 0; pass < a.platforms.size(); ++pass)
        {
            bool changed = false;
            for (std::size_t start = 0; start < a.platforms.size(); ++start)
                if (reached[start])
                {
                    const auto r = a.platforms[start];
                    for (float x = r.position.x; x <= r.position.x + r.size.x - 48; x += 32)
                    {
                        const Rect at{{x, r.position.y - 48}, {48, 48}};
                        bool blocked = false;
                        for (const auto& solid : a.platforms)
                            if (collision::overlaps(at, solid))
                                blocked = true;
                        if (blocked)
                            continue;
                        for (int direction : {-1, 0, 1})
                            for (int hold : {18, 36, 60, 90})
                            {
                                auto p = body(1, at.position);
                                p.applyInput({static_cast<float>(direction), true});
                                for (int tick = 0; tick < 160; ++tick)
                                {
                                    p.update(1.f / 120, a);
                                    if (p.grounded && tick > 1)
                                    {
                                        for (std::size_t dest = 0; dest < a.platforms.size(); ++dest)
                                        {
                                            const auto d = a.platforms[dest];
                                            if (near(p.position.y + 48, d.position.y) &&
                                                p.position.x + 48 > d.position.x &&
                                                p.position.x < d.position.x + d.size.x && !reached[dest])
                                            {
                                                reached[dest] = true;
                                                changed = true;
                                            }
                                        }
                                        break;
                                    }
                                    p.applyInput({tick < hold ? static_cast<float>(direction) : 0.f});
                                }
                            }
                    }
                }
            if (!changed)
                break;
        }
        if (!std::all_of(reached.begin(), reached.end(), [](bool b) { return b; }))
        {
            std::cerr << "Inaccessible map " << static_cast<unsigned>(info.id) << " surfaces:";
            for (std::size_t i = 0; i < reached.size(); ++i)
                if (!reached[i])
                    std::cerr << ' ' << i;
            std::cerr << '\n';
            throw std::runtime_error("Every platform must be reachable with current jump physics");
        }
    }
}

void selectionAndEvents()
{
    Match lan;
    lan.connect(1);
    lan.connect(2);
    expect(!lan.selectMap(2, MapId::FactorySmall, 0), "Only LAN host chooses map");
    lan.setReady(2, true);
    expect(lan.selectMap(1, MapId::NatureLarge, 0) && !lan.snapshot().ready[1],
           "Map change resets readiness");
    expect(!lan.selectMap(1, MapId::Duel, 0), "Stale map revision rejected");
    lan.setReady(1, true);
    lan.setReady(2, true);
    expect(lan.snapshot().map == MapId::NatureLarge &&
               !lan.selectMap(1, MapId::Duel, lan.snapshot().selectionRevision),
           "Map locked after countdown begins");
    Match online(true);
    online.connect(1);
    online.connect(2);
    online.setReady(1, true);
    online.setReady(2, true);
    expect(online.phase() == MatchPhase::Lobby, "Voting required before ready");
    expect(online.selectMap(1, MapId::RuinsMedium, 0), "First online vote");
    expect(online.selectMap(2, MapId::FactoryLarge, online.snapshot().selectionRevision),
           "Second online vote");
    online.setReady(1, true);
    online.setReady(2, true);
    expect(online.snapshot().map == MapId::RuinsMedium || online.snapshot().map == MapId::FactoryLarge,
           "Tied vote picks one of the voted maps");
    auto snapshot = network::encodeSnapshot(online.snapshot());
    expect(network::decodeSnapshot(snapshot)->map == online.snapshot().map, "Selected map survives protocol");
    snapshot.set_map(999);
    expect(!network::decodeSnapshot(snapshot), "Unknown map rejected");
    World w(flat());
    for (int i = 0; i < 5; ++i)
    {
        w.setPlayerInput(1, {});
        w.setPlayerInput(1, {0, false, true, {1, 0}});
        w.update(.4f);
    }
    auto events = w.takeEvents();
    int shots = 0, hits = 0, deaths = 0;
    std::uint64_t previous = 0;
    for (const auto& e : events)
    {
        expect(e.id > previous, "Event IDs are monotonic");
        previous = e.id;
        shots += e.kind == EventKind::Shot;
        hits += e.kind == EventKind::Hit;
        deaths += e.kind == EventKind::Death;
    }
    expect(shots == 5 && hits == 5 && deaths == 1,
           "Shot, hit and death events survive between presentation frames");
    expect(w.takeEvents().empty(), "Taking events consumes them");
    for (auto& e : events)
        e.round = 1;
    auto encoded = network::encodeEvents(events);
    expect(network::decodeEvents(encoded)->size() == events.size(), "Events protocol roundtrip");
    encoded.mutable_events(0)->set_kind(99);
    expect(!network::decodeEvents(encoded), "Malformed event rejected");
}
} // namespace

int main()
{
    try
    {
        bodies();
        maps();
        selectionAndEvents();
        std::cout << "Combat and map checks passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
