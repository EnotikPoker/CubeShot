#include "game/BotController.h"
#include "game/Collision.h"
#include <algorithm>
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

float distance(Vec2 a, Vec2 b)
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

void follow(Player& p, const BotNavigation& graph, const std::vector<BotTraversal>& path, const Arena& arena)
{
    for (const auto& edge : path)
    {
        const auto start = graph.nodes()[edge.from].position, end = graph.nodes()[edge.to].position;
        if (!edge.jump && graph.nodes()[edge.from].surface == graph.nodes()[edge.to].surface)
        {
            for (int tick = 0; tick < 120 && distance(p.position, end) > 2.5f; ++tick)
            {
                p.applyInput({end.x > p.position.x ? 1.f : -1.f});
                p.update(dt, arena);
            }
            expect(distance(p.position, end) < 3, "Walk route reaches its destination");
            continue;
        }
        for (int tick = 0; tick < 120 && distance(p.position, start) > .05f; ++tick)
        {
            p.applyInput({std::clamp((start.x - p.position.x) / (Player::moveSpeed * dt), -1.f, 1.f)});
            p.update(dt, arena);
        }
        expect(distance(p.position, start) < 3, "Jump launch can be reached from previous landing");
        bool airborne = false;
        for (int tick = 0; tick < edge.durationTicks + 30; ++tick)
        {
            p.applyInput(
                {tick < edge.holdTicks ? static_cast<float>(edge.direction) : 0.f, edge.jump && tick == 0});
            p.update(dt, arena);
            airborne = airborne || !p.grounded;
            if (airborne && p.grounded)
                break;
        }
        if (!(p.grounded && std::abs(p.position.y - end.y) < .1f && std::abs(p.position.x - end.x) < 20))
        {
            std::cerr << "map=" << static_cast<int>(arena.map) << " edge=" << edge.from << "->" << edge.to
                      << " start=" << start.x << ',' << start.y << " end=" << end.x << ',' << end.y
                      << " actual=" << p.position.x << ',' << p.position.y << " jump=" << edge.jump
                      << " dir=" << edge.direction << " hold=" << edge.holdTicks << '\n';
            throw std::runtime_error(
                "Verified airborne route lands on intended platform using normal physics");
        }
    }
}

void navigation()
{
    for (const auto& info : mapCatalog())
    {
        if (!validMap(info.id))
            continue;
        auto arena = Arena::create(info.id);
        auto graph = BotNavigation::forMap(info.id);
        expect(graph == BotNavigation::forMap(info.id), "Navigation is cached per map");
        expect(!graph->nodes().empty(), "Map has navigable positions");
        for (auto spawn : arena.spawnPoints)
            for (std::size_t surface = 0; surface < arena.platforms.size(); ++surface)
            {
                bool reached = false;
                for (const auto& node : graph->nodes())
                {
                    if (node.surface != surface)
                        continue;
                    auto route = graph->route(spawn, node.position);
                    if (route.empty() && distance(spawn, node.position) > 20)
                        continue;
                    Player p;
                    p.position = spawn;
                    follow(p, *graph, route, arena);
                    expect(std::abs(p.position.y - node.position.y) < .1f,
                           "Executed route reaches requested platform");
                    reached = true;
                    break;
                }
                if (!reached)
                {
                    std::cerr << "Map " << static_cast<int>(info.id) << " surface " << surface << '\n';
                    throw std::runtime_error("Every platform must be reachable from both spawns");
                }
            }
    }
}

void perception()
{
    auto arena = Arena::createDuel();
    World world(arena, 2);
    auto observation = observeBot(world, 2, 1);
    expect(observation.target.has_value(), "Whole arena is observable");
    BotController bot(arena, BotDifficulty::Medium, 42);
    for (int tick = 0; tick < 30; ++tick)
        expect(!bot.update(observation, dt).shoot, "No shot before difficulty reaction delay");
    bool shot = false;
    for (int tick = 0; tick < 120; ++tick)
        shot = bot.update(observation, dt).shoot || shot;
    expect(shot, "Bot fires after processing the opponent observation");
    observation.target.reset();
    for (int tick = 0; tick < 180; ++tick)
        expect(!bot.update(observation, dt).shoot, "No shooting at absent opponent");
    bot.reset();
    expect(!bot.update(observeBot(world, 2, 1), dt).shoot, "Reset restores reaction delay");
}

void platformEncounters()
{
    for (const auto& info : mapCatalog())
    {
        if (!validMap(info.id))
            continue;
        const auto graph = BotNavigation::forMap(info.id);
        auto arena = Arena::create(info.id);
        for (std::size_t surface = 1; surface < arena.platforms.size(); ++surface)
        {
            std::vector<Vec2> positions;
            for (const auto& node : graph->nodes())
                if (node.surface == surface)
                    positions.push_back(node.position);
            arena.spawnPoints[0] = positions[positions.size() / 2];
            World world(arena, 2);
            BotController bot(arena, BotDifficulty::Medium, 42 + static_cast<unsigned>(surface));
            for (int tick = 0; tick < 120 * 60 && world.players()[0].alive(); ++tick)
            {
                const auto input = bot.update(observeBot(world, 2, 1), dt);
                world.setPlayerInput(2, {input.moveDirection, false, false, input.aimDirection});
                world.requestAction(2, input.jump, input.shoot, input.aimDirection);
                world.update(dt);
                world.takeEvents();
            }
            if (world.players()[0].alive())
            {
                const auto& p = world.players()[1];
                std::cerr << "map=" << static_cast<int>(info.id) << " platform=" << surface
                          << " bot=" << p.position.x << ',' << p.position.y
                          << " hp=" << world.players()[0].health << '\n';
                throw std::runtime_error("Controller must find and defeat a target on each platform");
            }
        }
    }
}

void aiming()
{
    Arena arena{{{0, 0}, {1600, 900}}, {{{0, 800}, {1600, 100}}}, {{100, 752}, {900, 752}}, MapId::Duel};
    for (float targetX : {300.f, 650.f, 1200.f})
        for (float speed : {-120.f, 0.f, 120.f})
        {
            Player shooter, target;
            shooter.id = 1;
            shooter.position = {100, 752};
            target.id = 2;
            target.position = {targetX, 752};
            target.velocity.x = speed;
            auto shot = botAim(shooter, {target.position, target.velocity, target.size, true}, arena);
            expect(shot.has_value(), "Ballistic aim exists at supported distances");
            auto origin = shooter.weaponOrigin();
            Bullet bullet;
            bullet.position = {origin.x + shot->direction.x * 36, origin.y + shot->direction.y * 36};
            bullet.velocity = {shot->direction.x * 800, shot->direction.y * 800};
            bool hit = false;
            for (int tick = 0; tick < 360; ++tick)
            {
                auto start = bullet.position;
                target.update(dt, arena);
                bullet.update(dt);
                if (collision::segmentHit(start, bullet.position, target.bounds(), Bullet::radius))
                {
                    hit = true;
                    break;
                }
            }
            expect(hit, "Real bullet hits stationary/moving target with ballistic compensation");
        }
    Player shooter;
    shooter.position = {100, 752};
    BotTarget target{{900, 752}, {}, {48, 48}, true};
    arena.platforms.push_back({{600, 0}, {20, 800}});
    expect(!botAim(shooter, target, arena), "Blocked ballistic trajectory is rejected");
    const auto easy = botTuning(BotDifficulty::Easy), medium = botTuning(BotDifficulty::Medium),
               hard = botTuning(BotDifficulty::Hard);
    expect(easy.reaction > medium.reaction && medium.reaction > hard.reaction &&
               easy.aimErrorDegrees > medium.aimErrorDegrees &&
               medium.aimErrorDegrees > hard.aimErrorDegrees && hard.shotInterval >= .25f,
           "Difficulty changes skill without upgrading the pistol");
    auto duel = Arena::createDuel();
    BotController first(duel, BotDifficulty::Medium, 111), second(duel, BotDifficulty::Medium, 111);
    World world(duel, 2);
    auto observation = observeBot(world, 2, 1);
    int shots = 0;
    float since = 100;
    for (int tick = 0; tick < 1200; ++tick)
    {
        auto a = first.update(observation, dt), b = second.update(observation, dt);
        expect(a.moveDirection == b.moveDirection && a.jump == b.jump && a.shoot == b.shoot &&
                   a.aimDirection.x == b.aimDirection.x,
               "Fixed seed reproduces decisions");
        since += dt;
        if (a.shoot)
        {
            expect(since + .001f >= medium.shotInterval, "Shot pulses obey difficulty interval");
            since = 0;
            ++shots;
        }
    }
    expect(shots >= 5, "Bot actually fires multiple single shots");
}
} // namespace

int main()
{
    try
    {
        navigation();
        perception();
        aiming();
        platformEncounters();
        std::cout << "Bot navigation, perception and combat passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
