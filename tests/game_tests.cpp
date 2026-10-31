#include "game/Collision.h"
#include "game/World.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

bool near(float first, float second)
{
    return std::abs(first - second) < 0.01f;
}

void testMovement()
{
    using namespace cubeshot;
    World world;
    expect(world.players().size() == 4 && world.bullets().empty(), "Player and three targets");
    auto solo = world.arena();
    solo.spawnPoints.resize(1);
    world = World{solo};
    const Player& player = world.players().front();
    const float startX = player.position.x;
    const float startY = player.position.y;
    expect(near(startY + player.size.y, world.arena().platforms.front().position.y),
           "Player must spawn on the floor");

    world.setPlayerInput(World::localPlayerId, PlayerInput{1.0f});
    world.update(0.5f);
    expect(near(player.position.x, startX + Player::moveSpeed * 0.5f), "Move right");
    expect(near(player.position.y, startY), "Horizontal movement stays on the floor");

    world.setPlayerInput(World::localPlayerId, {});
    const float stoppedX = player.position.x;
    world.update(0.5f);
    expect(near(player.position.x, stoppedX), "Release stops movement");

    world.setPlayerInput(World::localPlayerId, PlayerInput{-1.0f});
    world.update(0.5f);
    expect(near(player.position.x, startX), "Move left");
    world.update(10.0f);
    expect(near(player.position.x, 0.0f), "Left boundary");
    world.setPlayerInput(World::localPlayerId, PlayerInput{1.0f});
    world.update(10.0f);
    expect(near(player.position.x + player.size.x, world.arena().bounds.size.x), "Right boundary");

    world.setPlayerInput(World::localPlayerId, {});
    world.setPlayerInput(999, PlayerInput{-1.0f});
    const float beforeUnknownInput = player.position.x;
    world.update(0.1f);
    expect(near(player.position.x, beforeUnknownInput), "Unknown player input is ignored");
}

void testTimeSteps()
{
    using namespace cubeshot;
    World slowFrames;
    World fastFrames;
    slowFrames.setPlayerInput(World::localPlayerId, PlayerInput{-1.0f});
    fastFrames.setPlayerInput(World::localPlayerId, PlayerInput{-1.0f});
    for (int i = 0; i < 30; ++i)
    {
        slowFrames.update(1.0f / 30.0f);
    }
    for (int i = 0; i < 120; ++i)
    {
        fastFrames.update(1.0f / 120.0f);
    }
    expect(near(slowFrames.players().front().position.x, fastFrames.players().front().position.x),
           "Movement must be independent of update frequency");

    const float before = fastFrames.players().front().position.x;
    fastFrames.update(0.0f);
    fastFrames.update(-1.0f);
    expect(near(fastFrames.players().front().position.x, before), "Non-positive time cannot move player");
}

void testCollisions()
{
    using namespace cubeshot;
    const Rect arena{{0.0f, 0.0f}, {960.0f, 540.0f}};
    const std::vector<Rect> walls{{{200.0f, 0.0f}, {10.0f, 440.0f}}};
    const Rect left{{100.0f, 392.0f}, {48.0f, 48.0f}};
    const Rect right{{300.0f, 392.0f}, {48.0f, 48.0f}};
    expect(near(collision::moveHorizontally(left, 700.0f, arena, walls), 152.0f),
           "Cannot cross a thin wall moving right");
    expect(near(collision::moveHorizontally(right, -700.0f, arena, walls), 210.0f),
           "Cannot cross a thin wall moving left");
    const Rect floor{{0.0f, 440.0f}, {960.0f, 100.0f}};
    expect(!collision::overlaps(left, floor), "Touching the floor is not penetration");
    expect(collision::overlaps({{100.0f, 400.0f}, {48.0f, 48.0f}}, floor),
           "Overlapping rectangles are detected");
    expect(near(collision::moveHorizontally(left, 100.0f, arena, {floor}), 200.0f),
           "Floor does not block horizontal movement");
}

void testInputAndWeaponState()
{
    using namespace cubeshot;
    Player player;
    player.applyInput(PlayerInput{5.0f});
    expect(near(player.velocity.x, Player::moveSpeed), "Input cannot exceed maximum speed");
    Weapon weapon;
    weapon.cooldown = 0.25f;
    weapon.update(0.1f);
    expect(near(weapon.cooldown, 0.15f), "Weapon timer decreases in seconds");
    weapon.update(1.0f);
    expect(near(weapon.cooldown, 0.0f), "Weapon timer cannot become negative");
}

void testJump()
{
    using namespace cubeshot;
    constexpr float step = 1.0f / 120.0f;
    World world;
    const Player& player = world.players().front();
    const float floorY = player.position.y;
    expect(player.grounded, "Player starts grounded");
    world.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
    world.update(0.0f);
    world.update(-1.0f);
    expect(near(player.position.y, floorY), "Invalid time does not consume or execute jump");
    world.update(step);
    expect(player.position.y < floorY && player.velocity.y < 0.0f && !player.grounded,
           "Space input starts an upward jump from the floor");
    const float firstVelocity = player.velocity.y;
    world.setPlayerInput(World::localPlayerId, {});
    world.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
    world.update(step);
    expect(player.velocity.y > firstVelocity, "Gravity slows ascent and air jump is ignored");

    float highestY = player.position.y;
    bool fell = false;
    for (int i = 0; i < 180; ++i)
    {
        world.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
        world.update(step);
        highestY = std::min(highestY, player.position.y);
        fell = fell || player.velocity.y > 0.0f;
    }
    expect(floorY - highestY > 100.0f && floorY - highestY < 110.0f, "Jump reaches the intended height");
    expect(fell && player.grounded && near(player.position.y, floorY) && near(player.velocity.y, 0.0f),
           "Player falls, lands and does not jump repeatedly while holding Space");
    world.setPlayerInput(World::localPlayerId, {});
    world.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
    world.setPlayerInput(World::localPlayerId, {});
    world.update(step);
    expect(player.velocity.y < 0.0f, "A fresh short press survives until the next physics step");
}

void testPlatform()
{
    using namespace cubeshot;
    constexpr float step = 1.0f / 120.0f;
    auto solo = Arena::createDefault();
    solo.spawnPoints.resize(1);
    World world(solo);
    const Player& player = world.players().front();
    expect(world.arena().platforms.size() == 2, "Default map includes a test platform");
    const Rect platform = world.arena().platforms.back();
    world.setPlayerInput(World::localPlayerId, PlayerInput{1.0f, true});
    for (int i = 0; i < 58; ++i)
    {
        world.update(step);
        expect(!collision::overlaps(player.bounds(), platform), "Jump must not penetrate platform");
    }
    world.setPlayerInput(World::localPlayerId, {});
    world.update(0.2f);
    expect(player.grounded && near(player.position.y + player.size.y, platform.position.y),
           "Platform is reachable by jumping right from spawn");
    world.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
    world.update(step);
    expect(player.velocity.y < 0.0f && !player.grounded, "Player can jump from platform");
    world.setPlayerInput(World::localPlayerId, {});
    world.update(1.0f);
    expect(player.grounded && near(player.position.y + player.size.y, platform.position.y),
           "Large update preserves jump and landing");
    world.setPlayerInput(World::localPlayerId, PlayerInput{1.0f});
    bool falling = false;
    for (int i = 0; i < 180; ++i)
    {
        world.update(step);
        falling = falling || (!player.grounded && player.velocity.y > 0.0f);
    }
    expect(falling && player.grounded &&
               near(player.position.y + player.size.y, world.arena().platforms.front().position.y),
           "Walking off platform causes falling and landing on floor");

    Player underPlatform;
    underPlatform.position = {platform.position.x + 20.0f, 392.0f};
    underPlatform.applyInput(PlayerInput{0.0f, true});
    for (int i = 0; i < 6; ++i)
    {
        underPlatform.update(step, world.arena());
    }
    expect(underPlatform.position.y >= platform.position.y + platform.size.y &&
               underPlatform.velocity.y >= 0.0f && !underPlatform.grounded,
           "Hitting underside stops ascent without granting another jump");
    underPlatform.applyInput({});
    underPlatform.applyInput(PlayerInput{0.0f, true});
    underPlatform.update(step, world.arena());
    expect(underPlatform.velocity.y > 0.0f, "Cannot jump after hitting platform underside");
}

void testVerticalCollisions()
{
    using namespace cubeshot;
    const Rect arena{{0.0f, 0.0f}, {960.0f, 540.0f}};
    const std::vector<Rect> platforms{{{100.0f, 200.0f}, {200.0f, 8.0f}}, {{100.0f, 350.0f}, {200.0f, 8.0f}}};
    const auto down = collision::moveVertically({{120.0f, 20.0f}, {48.0f, 48.0f}}, 1000.0f, arena, platforms);
    expect(down.blocked && near(down.y, 152.0f), "Fast fall lands on nearest thin platform");
    const auto up = collision::moveVertically({{120.0f, 400.0f}, {48.0f, 48.0f}}, -1000.0f, arena, platforms);
    expect(up.blocked && near(up.y, 358.0f), "Fast ascent hits nearest platform underside");
    const auto ceiling =
        collision::moveVertically({{500.0f, 100.0f}, {48.0f, 48.0f}}, -1000.0f, arena, platforms);
    expect(ceiling.blocked && near(ceiling.y, 0.0f), "Arena ceiling blocks ascent");
    const auto bottom =
        collision::moveVertically({{500.0f, 100.0f}, {48.0f, 48.0f}}, 1000.0f, arena, platforms);
    expect(bottom.blocked && near(bottom.y, 492.0f), "Arena bottom blocks falling");
    expect(!collision::isSupported({{300.0f, 152.0f}, {48.0f, 48.0f}}, arena, platforms),
           "Touching platform side does not count as support");

    World slow;
    World fast;
    slow.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
    fast.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
    for (int i = 0; i < 9; ++i)
        slow.update(1.0f / 30.0f);
    for (int i = 0; i < 36; ++i)
        fast.update(1.0f / 120.0f);
    expect(near(slow.players().front().position.y, fast.players().front().position.y) &&
               near(slow.players().front().velocity.y, fast.players().front().velocity.y),
           "Jump physics agree at 30 and 120 updates per second");
}

void testSingleShots()
{
    using namespace cubeshot;
    constexpr float step = 1.0f / 120.0f;
    World world;
    PlayerInput input;
    input.aimDirection = {0, -1};
    input.shoot = true;
    world.setPlayerInput(World::localPlayerId, input);
    world.update(0);
    world.update(-1);
    expect(world.bullets().empty(), "Invalid time does not execute shot");
    world.update(step);
    expect(world.bullets().size() == 1, "One bullet per press");
    expect(world.bullets().front().damage == 20, "Pistol bullet deals 20 damage");
    expect(near(world.bullets().front().position.y, world.players().front().muzzlePosition().y),
           "Bullet starts at muzzle");
    for (int i = 0; i < 36; ++i)
    {
        world.setPlayerInput(World::localPlayerId, input);
        world.update(step);
    }
    expect(world.bullets().size() == 1, "Holding trigger does not repeat after cooldown");

    input.shoot = false;
    world.setPlayerInput(World::localPlayerId, input);
    input.shoot = true;
    world.setPlayerInput(World::localPlayerId, input);
    input.shoot = false;
    world.setPlayerInput(World::localPlayerId, input);
    world.update(step);
    expect(world.bullets().size() == 2, "Short click between physics steps survives release");
    input.shoot = true;
    world.setPlayerInput(World::localPlayerId, input);
    world.update(step);
    expect(world.bullets().size() == 2, "Cooldown rejects early click");
    world.update(4.0f);
    expect(world.bullets().empty(), "Old bullets expire or leave arena; no delayed automatic shot");
    expect(world.players().front().health == 100, "Shooter is immune to own bullets");
}

void testSimultaneousActions()
{
    using namespace cubeshot;
    constexpr float step = 1.0f / 120.0f;
    World world;
    const Player& player = world.players().front();
    const Vec2 start = player.position;
    PlayerInput input;
    input.moveDirection = 1.0f;
    input.jump = true;
    input.shoot = true;
    input.aimDirection = {-3, -4};
    world.setPlayerInput(World::localPlayerId, input);
    world.update(step);
    expect(player.position.x > start.x && player.position.y < start.y && near(player.aimDirection.x, -0.6f) &&
               near(player.aimDirection.y, -0.8f) && world.bullets().size() == 1 &&
               world.bullets().front().velocity.x < 0,
           "Walking, jumping, aiming left and shooting work in the same step");

    input.jump = false;
    input.shoot = false;
    input.aimDirection = {3, -4};
    world.setPlayerInput(World::localPlayerId, input);
    world.update(step);
    expect(player.position.x > start.x + Player::moveSpeed * step && player.velocity.y < 0 &&
               near(player.aimDirection.x, 0.6f) && world.bullets().size() == 1,
           "Changing aim and releasing trigger do not stop movement or repeat shot");
}

void testBulletPhysics()
{
    using namespace cubeshot;
    Bullet bullet;
    bullet.position = {100, 100};
    bullet.velocity = {800, 0};
    bullet.update(0.5f);
    expect(near(bullet.position.x, 500) && near(bullet.position.y, 130) && near(bullet.velocity.y, 120),
           "Visible projectile has flight time and gravity");
    expect(near(bullet.lifetime, 2.5f), "Bullet lifetime decreases");
    bullet.update(0);
    bullet.update(-1);
    expect(near(bullet.position.x, 500), "Invalid time does not move bullet");

    World slow;
    World fast;
    PlayerInput input;
    input.shoot = true;
    input.aimDirection = {0, -1};
    slow.setPlayerInput(1, input);
    fast.setPlayerInput(1, input);
    for (int i = 0; i < 6; ++i)
        slow.update(1.0f / 30.0f);
    for (int i = 0; i < 24; ++i)
        fast.update(1.0f / 120.0f);
    expect(slow.bullets().size() == 1 && fast.bullets().size() == 1, "Both shots are in flight");
    expect(near(slow.bullets().front().position.y, fast.bullets().front().position.y),
           "Bullet physics agree at 30 and 120 updates per second");
}

cubeshot::Arena shootingArena()
{
    cubeshot::Arena arena;
    arena.bounds = {{0, 0}, {2000, 1200}};
    arena.platforms = {{{0, 1000}, {2000, 200}}};
    // Дальний игрок раньше ближнего: попадание зависит от расстояния, а не порядка.
    arena.spawnPoints = {{100, 952}, {400, 952}, {250, 952}};
    return arena;
}

void testBulletHits()
{
    using namespace cubeshot;
    PlayerInput input;
    input.shoot = true;
    World world(shootingArena());
    world.setPlayerInput(1, input);
    world.update(0.05f);
    expect(world.players()[2].alive() && world.bullets().size() == 1,
           "Damage waits for bullet to reach target");
    world.update(0.3f);
    expect(world.players()[2].health == 80 && world.players()[1].health == 100 &&
               world.players()[0].health == 100 && world.bullets().empty(),
           "Only nearest target loses 20 health; bullet is consumed");
    for (int hits = 2; hits <= 5; ++hits)
    {
        world.setPlayerInput(1, {});
        world.setPlayerInput(1, input);
        world.update(0.35f);
        expect(world.players()[2].health == Player::maxHealth - hits * 20 &&
                   world.players()[1].health == Player::maxHealth && world.bullets().empty(),
               "Nearest target takes five pistol hits to die and shields target behind it");
    }
    world.setPlayerInput(3, PlayerInput{1, true, true});
    world.update(0.5f);
    expect(near(world.players()[2].position.x, 250) && world.bullets().empty(),
           "Dead player cannot move, jump or fire");
    bool hitRemains = false;
    int previousHealth = world.players()[1].health;
    for (int shots = 0; shots < 120 && world.players()[1].alive(); ++shots)
    {
        world.setPlayerInput(1, {});
        world.setPlayerInput(1, input);
        world.update(.4f);
        const int health = world.players()[1].health;
        expect(health <= previousHealth && previousHealth - health <= 20,
               "A bullet hits either remains or the next target, never both");
        previousHealth = health;
        for (const auto& event : world.takeEvents())
            hitRemains =
                hitRemains || event.kind == EventKind::CorpseHit || event.kind == EventKind::CorpseBreak;
    }
    expect(hitRemains && !world.players()[1].alive(),
           "Physical remains absorb shots and can be broken or pushed out of the way");
    world = World(shootingArena());
    expect(world.bullets().empty() && world.players()[1].health == Player::maxHealth &&
               world.players()[2].health == Player::maxHealth,
           "Resetting world restores full health and living targets");

    auto arena = shootingArena();
    arena.platforms.push_back({{200, 800}, {1, 200}});
    World wall(arena);
    wall.setPlayerInput(1, input);
    wall.update(0.4f);
    expect(wall.players()[2].alive() && wall.bullets().empty(), "Thin wall shields target");
    arena.platforms.back().position.x = 152;
    World muzzleWall(arena);
    muzzleWall.setPlayerInput(1, input);
    muzzleWall.update(1.0f / 120.0f);
    expect(muzzleWall.bullets().empty() && muzzleWall.players()[2].alive(),
           "Gun protruding through wall cannot shoot through it");

    World floor(shootingArena());
    input.aimDirection = {0, 1};
    floor.setPlayerInput(1, input);
    floor.update(0.1f);
    expect(floor.bullets().empty(), "Floor absorbs bullets even beside muzzle");
}

void testBulletSweepsAndAim()
{
    using namespace cubeshot;
    const Rect thinWall{{500, 100}, {1, 200}};
    const auto hit = collision::segmentHit({0, 150}, {1000, 150}, thinWall, Bullet::radius);
    expect(hit && near(*hit, 0.497f), "Swept bullet cannot tunnel through thin wall");
    expect(!collision::segmentHit({0, 50}, {1000, 50}, thinWall, Bullet::radius),
           "Near miss is not a collision");
    expect(collision::segmentHit({500, 150}, {500, 150}, thinWall).has_value(),
           "Bullet starting inside obstacle is blocked");
    expect(collision::segmentHit({800, 400}, {200, 0}, thinWall).has_value(),
           "Diagonal reverse sweep hits obstacle");

    Player player;
    PlayerInput input;
    input.aimDirection = {3, 4};
    player.applyInput(input);
    expect(near(player.aimDirection.x, 0.6f) && near(player.aimDirection.y, 0.8f),
           "Aim is normalized so diagonal shots have same speed");
    input.aimDirection = {};
    player.applyInput(input);
    expect(near(player.aimDirection.x, 0.6f), "Zero aim preserves last valid direction");
    player.takeDamage(100);
    expect(!player.alive() && player.health == 0, "Damage equal to maximum health kills Player");

    World world;
    const auto initial = world.players();
    world.update(2);
    for (std::size_t i = 1; i < initial.size(); ++i)
    {
        expect(near(initial[i].position.x, world.players()[i].position.x) &&
                   near(initial[i].position.y, world.players()[i].position.y),
               "Targets stay stationary on floor and platform");
    }
}

void testHealth()
{
    using namespace cubeshot;
    Player player;
    expect(player.health == 100 && Player::maxHealth == 100, "Players start with maximum 100 health");
    player.applyInput(PlayerInput{1.0f});
    player.takeDamage(20);
    expect(player.health == 80 && player.alive() && player.velocity.x > 0,
           "Nonlethal damage does not disable a living player");
    player.takeDamage(0);
    player.takeDamage(-20);
    expect(player.health == 80, "Zero or negative damage cannot heal a player");
    player.takeDamage(std::numeric_limits<int>::max());
    expect(player.health == 0 && !player.alive() && near(player.velocity.x, 0),
           "Overkill clamps health to zero and stops movement");
    player.takeDamage(20);
    expect(player.health == 0, "Health cannot become negative after death");
}

void testCancelInput()
{
    using namespace cubeshot;
    World world;
    PlayerInput input;
    input.moveDirection = 1;
    input.jump = input.shoot = true;
    input.aimDirection = {-1, 0};
    world.setPlayerInput(1, input);
    const Vec2 start = world.players().front().position;
    world.clearPlayerInput(1);
    world.clearPlayerInput(999);
    world.update(1.0f / 120.0f);
    expect(world.players().front().position.x == start.x && world.players().front().position.y == start.y &&
               world.bullets().empty() && world.players().front().aimDirection.x == -1,
           "Opening menu cancels queued movement, jump and shot while preserving aim");
    world.setPlayerInput(1, input);
    world.update(1.0f / 120.0f);
    const float vertical = world.players().front().velocity.y;
    world.clearPlayerInput(1);
    expect(world.players().front().velocity.y == vertical,
           "Clearing input does not change vertical momentum for resume");
}

} // namespace

int main()
{
    try
    {
        testMovement();
        testTimeSteps();
        testCollisions();
        testInputAndWeaponState();
        testJump();
        testPlatform();
        testVerticalCollisions();
        testSingleShots();
        testSimultaneousActions();
        testBulletPhysics();
        testBulletHits();
        testBulletSweepsAndAim();
        testHealth();
        testCancelInput();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Game tests passed\n";
    return 0;
}
