#include "game/BotMatch.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

namespace
{
constexpr float dt = 1.f / 120;

void expect(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void frameCadence()
{
    BotMatch reference(MapId::Duel, BotDifficulty::Medium, "FPS", 42);
    for (int tick = 0; tick < 120 * 8; ++tick)
        reference.update(dt);
    const auto expected = reference.snapshot();
    for (int fps : {30, 60, 240})
    {
        BotMatch game(MapId::Duel, BotDifficulty::Medium, "FPS", 42);
        for (int frame = 0; frame < fps * 8; ++frame)
            game.update(1.f / fps);
        auto actual = game.snapshot();
        expect(actual.tick == expected.tick && actual.phase == expected.phase &&
                   actual.round == expected.round && actual.score == expected.score &&
                   actual.bullets.size() == expected.bullets.size(),
               "Bot decisions and match rules are independent of rendering cadence");
        for (std::size_t i = 0; i < actual.players.size(); ++i)
            expect(actual.players[i].health == expected.players[i].health &&
                       std::abs(actual.players[i].position.x - expected.players[i].position.x) < .01f &&
                       std::abs(actual.players[i].position.y - expected.players[i].position.y) < .01f,
                   "Bot position and damage are identical at 30/60/120/240 FPS");
    }
}

void matches()
{
    for (const auto& info : mapCatalog())
    {
        if (!validMap(info.id))
            continue;
        for (auto difficulty : {BotDifficulty::Easy, BotDifficulty::Medium, BotDifficulty::Hard})
        {
            BotMatch game(info.id, difficulty, "Проверка", 42);
            expect(game.snapshot().phase == MatchPhase::Countdown && game.snapshot().map == info.id &&
                       game.snapshot().names[0] == "Проверка" && game.snapshot().names[1] == "Бот",
                   "Autonomous match starts with correct map, names and countdown");
            bool shot = false, moved = false, hit = false;
            for (int tick = 0; tick < 120 * 45; ++tick)
            {
                game.update(dt);
                const auto s = game.snapshot();
                moved =
                    moved || std::abs(s.players[1].position.x - Arena::create(info.id).spawnPoints[1].x) > 30;
                hit = hit || s.players[0].health < 100;
                for (const auto& event : game.takeEvents())
                    shot = shot || event.kind == EventKind::Shot;
                if (s.score[1] > 0)
                    break;
            }
            if (!shot || !moved || !hit || game.snapshot().score[1] == 0)
            {
                auto s = game.snapshot();
                std::cerr << "map=" << static_cast<int>(info.id)
                          << " difficulty=" << static_cast<int>(difficulty) << " moved=" << moved
                          << " shot=" << shot << " hp=" << s.players[0].health
                          << " bot=" << s.players[1].position.x << ',' << s.players[1].position.y << '\n';
                throw std::runtime_error(
                    "Bot must navigate, fire and defeat stationary opponent on every map");
            }
        }
    }
    BotMatch game(MapId::Duel, BotDifficulty::Medium, "Player", 25);
    bool sawSwap = false;
    std::uint32_t lastRound = 0;
    for (int tick = 0; tick < 120 * 240 && game.snapshot().phase != MatchPhase::Finished; ++tick)
    {
        if (game.snapshot().phase == MatchPhase::Armory)
            game.chooseWeapon(false);
        game.update(dt);
        auto s = game.snapshot();
        if (s.round != lastRound)
        {
            if (lastRound != 0)
                sawSwap = sawSwap || s.players[0].position.x > s.players[1].position.x;
            lastRound = s.round;
        }
        game.takeEvents();
    }
    auto finished = game.snapshot();
    expect(finished.phase == MatchPhase::Finished && finished.score[1] == 5 && finished.winner == 2 &&
               sawSwap,
           "Complete autonomous match reaches five wins with alternating spawns");
    game.rematch("Новый ник");
    auto restart = game.snapshot();
    expect(restart.phase == MatchPhase::Countdown && restart.score[1] == 0 && restart.map == MapId::Duel &&
               restart.players[0].health == 100 && restart.bullets.empty() && restart.names[0] == "Новый ник",
           "Rematch resets match on same map");
    game.update(3);
    expect(game.snapshot().phase == MatchPhase::Round, "Rematch countdown completes");
    game.action(false, true, {1, 0});
    game.clearInput();
    game.update(dt);
    bool humanShot = false;
    for (const auto& e : game.takeEvents())
        humanShot = humanShot || (e.kind == EventKind::Shot && e.actor == 1);
    expect(!humanShot, "Pausing cancels pending human actions");
    const auto before = game.snapshot();
    game.update(0);
    game.update(-1);
    expect(game.snapshot().tick == before.tick, "Invalid/nonadvancing timestep leaves match untouched");
    BotMatch split(MapId::Duel, BotDifficulty::Medium, "Split", 42);
    BotMatch whole(MapId::Duel, BotDifficulty::Medium, "Whole", 42);
    for (int i = 0; i < 120; ++i)
        whole.update(dt);
    for (int i = 0; i < 240; ++i)
        split.update(dt / 2);
    expect(whole.snapshot().tick == 120 && split.snapshot().tick == 120 &&
               std::abs(whole.snapshot().remaining - split.snapshot().remaining) < .001f,
           "Fixed simulation ticks do not depend on caller timestep or floating-point residue");
    bool rejected = false;
    try
    {
        BotMatch invalid(MapId::Test, BotDifficulty::Medium, "Player", 1);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    expect(rejected, "Target-only test map is not a bot duel arena");
}
} // namespace

int main()
{
    try
    {
        matches();
        frameCadence();
        std::cout << "Autonomous bot matches passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
