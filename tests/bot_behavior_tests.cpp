#include "game/BotController.h"
#include "game/Collision.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace cubeshot;

namespace
{
constexpr float dt = 1.f / 120;

void expect(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

Arena flat()
{
    return {{{0, 0}, {1200, 540}}, {{{0, 440}, {1200, 100}}}, {{100, 392}, {900, 392}}, MapId::Duel};
}

void apply(Player& self, const PlayerInput& input, const Arena& arena)
{
    self.applyInput({input.moveDirection, false, false, input.aimDirection});
    self.requestAction(input.jump, input.shoot, input.aimDirection);
    self.update(dt, arena);
    self.takeShot();
}

void knowledge()
{
    auto arena = flat();
    arena.platforms.push_back({{450, 0}, {20, 440}});
    World world(arena, 2);
    world.requestAction(1, false, true, {1, 0});
    world.requestAction(2, false, true, {-1, 0});
    world.update(dt);
    auto observation = observeBot(world, 2, 1);
    expect(observation.target && observation.target->position.x == 100,
           "Full-map view includes opponent behind solid terrain");
    expect(observation.bullets.size() == 1 && observation.bullets[0].shooterId == 1,
           "Full-map view includes enemy bullets behind terrain and excludes own bullets");
    expect(!botAim(observation.self, *observation.target, arena),
           "Knowing enemy position never permits shooting through a wall");
    world.setPlayerInput(1, {1});
    world.update(.1f);
    expect(observeBot(world, 2, 1).target->position.x > observation.target->position.x,
           "Movement behind cover remains observable");
}

void noHopping()
{
    for (const auto& info : mapCatalog())
    {
        if (!validMap(info.id))
            continue;
        auto arena = Arena::create(info.id);
        auto graph = BotNavigation::forMap(info.id);
        for (int side : {0, 1})
        {
            auto path = graph->route(arena.spawnPoints[side], arena.spawnPoints[1 - side]);
            expect(!path.empty(), "Spawn-to-spawn floor path exists");
            for (auto edge : path)
                expect(!edge.jump && graph->nodes()[edge.from].surface == 0 &&
                           graph->nodes()[edge.to].surface == 0,
                       "An unobstructed floor route uses walking, never repeated ceiling jumps");
        }
    }
    auto arena = flat();
    BotNavigation graph(arena);
    for (auto edge : graph.edges())
        expect(!edge.jump, "Flat unobstructed arena has no unnecessary jump edges");
    for (auto level : {BotDifficulty::Easy, BotDifficulty::Medium, BotDifficulty::Hard})
        for (unsigned seed : {1u, 42u, 123u})
        {
            BotController bot(arena, level, seed);
            Player self;
            self.id = 2;
            self.position = {900, 392};
            self.grounded = true;
            float settledX = 0;
            for (int tick = 0; tick < 120 * 35; ++tick)
            {
                // Oscillating human movement without firing. Holding position at the end
                // must let the bot settle rather than chase a preferred point forever.
                const float cycle = static_cast<float>(tick % 1440) / 1440;
                const float x = tick < 120 * 20 ? 100 + 550 * (1 - std::abs(2 * cycle - 1)) : 300;
                const float speed = tick < 120 * 20 ? (cycle < .5f ? 550.f / 6 : -550.f / 6) : 0.f;
                BotObservation observation{self, BotTarget{{x, 392}, {speed, 0}, {48, 48}, true}, {}};
                auto input = bot.update(observation, dt);
                expect(!input.jump, "No bullets or obstacles: moving opponent must not cause hopping");
                auto before = self.position;
                apply(self, input, arena);
                expect(std::abs(self.position.y - 392) < .01f &&
                           std::abs(self.position.x - before.x) <= Player::moveSpeed * dt + .001f,
                       "Navigation obeys normal physics and speed limits");
                if (tick == 120 * 28)
                    settledX = self.position.x;
                if (tick > 120 * 28)
                    expect(std::abs(self.position.x - settledX) < 3,
                           "A safe firing distance is stable after opponent stops moving");
            }
        }
}

struct AvoidanceResult
{
    int jumps = 0;
    bool hit = false, moved = false;
};

AvoidanceResult projectile(Arena arena, BotDifficulty level, Bullet bullet, int removeAt = 10000,
                           Vec2 spawn = {900, 392}, std::optional<BotTarget> opponent = {})
{
    BotController bot(arena, level, 42);
    Player self;
    self.id = 2;
    self.position = spawn;
    self.grounded = true;
    AvoidanceResult result;
    bool active = true;
    for (int tick = 0; tick < 120 * 4; ++tick)
    {
        BotObservation observation{self, opponent, {}};
        if (active && tick < removeAt)
            observation.bullets.push_back(bullet);
        auto input = bot.update(observation, dt);
        result.jumps += input.jump && self.grounded ? 1 : 0;
        apply(self, input, arena);
        result.moved =
            result.moved || std::abs(self.position.x - spawn.x) > 1 || self.position.y < spawn.y - 2;
        if (active && tick < removeAt)
        {
            auto start = bullet.position;
            bullet.update(dt);
            float barrier = 2;
            for (auto solid : arena.platforms)
                if (auto hit = collision::segmentHit(start, bullet.position, solid, Bullet::radius))
                    barrier = std::min(barrier, *hit);
            auto hit = collision::segmentHit(start, bullet.position, self.bounds(), Bullet::radius);
            if (bullet.lifetime > 0 && hit && *hit < barrier)
            {
                result.hit = true;
                active = false;
            }
            if (barrier <= 1 || bullet.lifetime <= 0 || bullet.position.x < 0 || bullet.position.x > 1200)
                active = false;
        }
    }
    return result;
}

Bullet incoming()
{
    Bullet bullet;
    bullet.id = 10;
    bullet.shooterId = 1;
    bullet.position = {100, 420};
    bullet.velocity = {800, -120};
    return bullet;
}

void avoidance()
{
    for (auto level : {BotDifficulty::Easy, BotDifficulty::Medium, BotDifficulty::Hard})
    {
        auto danger = projectile(flat(), level, incoming());
        expect(danger.moved && !danger.hit, "Real incoming projectile is avoided using predicted movement");
        expect(danger.jumps <= 1, "One projectile cannot trigger a chain of jumps after landing");
        auto besideLedge = incoming();
        besideLedge.velocity.y = -Bullet::gravity * ((560.f - besideLedge.position.x) / 800) / 2;
        auto recovered = projectile(Arena::createDuel(), level, besideLedge, 10000, {560, 392},
                                    BotTarget{{220, 392}, {}, {48, 48}, true});
        if (recovered.jumps > 1 || recovered.hit)
            std::cerr << "Recovery difficulty=" << static_cast<int>(level) << " jumps=" << recovered.jumps
                      << " hit=" << recovered.hit << '\n';
        expect(recovered.jumps <= 1 && !recovered.hit,
               "After a dodge beside a ledge the bot settles instead of chasing its airborne position");
        auto ghost = projectile(flat(), level, incoming(), 12);
        expect(!ghost.moved && ghost.jumps == 0, "Consumed projectile cannot cause delayed phantom dodging");
        auto expired = incoming();
        expired.lifetime = .05f;
        expect(projectile(flat(), level, expired).jumps == 0, "Expired projectile does not cause a jump");
        auto receding = incoming();
        receding.position = {700, 420};
        receding.velocity = {-800, -120};
        expect(!projectile(flat(), level, receding).moved, "Receding projectile causes no evasive action");
        auto high = incoming();
        high.position.y = 50;
        high.velocity.y = 0;
        expect(!projectile(flat(), level, high).moved, "A projectile passing high overhead is not a threat");
        auto shielded = flat();
        shielded.platforms.push_back({{700, 200}, {20, 240}});
        expect(!projectile(shielded, level, incoming()).moved,
               "A projectile stopped by terrain causes no evasive action");
        auto ceiling = flat();
        ceiling.platforms.push_back({{800, 350}, {220, 40}});
        expect(projectile(ceiling, level, incoming()).jumps == 0,
               "Bot does not headbutt a low ceiling when jumping cannot avoid the bullet");
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        const std::string only = argc > 1 ? argv[1] : "";
        if (only.empty() || only == "knowledge")
            knowledge();
        if (only.empty() || only == "hopping")
            noHopping();
        if (only.empty() || only == "avoidance")
            avoidance();
        std::cout << "Full-map observation, stable navigation and realistic avoidance passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
