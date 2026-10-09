#include "game/Collision.h"
#include "game/World.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
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
    expect(world.players().size() == 1 && world.bullets().empty(), "Initial world contents");
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
    for (int i = 0; i < 30; ++i) {
        slowFrames.update(1.0f / 30.0f);
    }
    for (int i = 0; i < 120; ++i) {
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
    PlayerInput reservedInput;
    reservedInput.shoot = true;
    World world;
    world.setPlayerInput(World::localPlayerId, reservedInput);
    const float startY = world.players().front().position.y;
    world.update(0.1f);
    expect(world.bullets().empty() && near(world.players().front().position.y, startY),
           "Shooting remains unimplemented");

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
    for (int i = 0; i < 180; ++i) {
        world.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
        world.update(step);
        highestY = std::min(highestY, player.position.y);
        fell = fell || player.velocity.y > 0.0f;
    }
    expect(floorY - highestY > 100.0f && floorY - highestY < 110.0f,
           "Jump reaches the intended height");
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
    World world;
    const Player& player = world.players().front();
    expect(world.arena().platforms.size() == 2, "Default map includes a test platform");
    const Rect platform = world.arena().platforms.back();
    world.setPlayerInput(World::localPlayerId, PlayerInput{1.0f, true});
    for (int i = 0; i < 58; ++i) {
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
    for (int i = 0; i < 180; ++i) {
        world.update(step);
        falling = falling || (!player.grounded && player.velocity.y > 0.0f);
    }
    expect(falling && player.grounded &&
               near(player.position.y + player.size.y, world.arena().platforms.front().position.y),
           "Walking off platform causes falling and landing on floor");

    Player underPlatform;
    underPlatform.position = {platform.position.x + 20.0f, 392.0f};
    underPlatform.applyInput(PlayerInput{0.0f, true});
    for (int i = 0; i < 6; ++i) {
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
    const std::vector<Rect> platforms{{{100.0f, 200.0f}, {200.0f, 8.0f}},
                                      {{100.0f, 350.0f}, {200.0f, 8.0f}}};
    const auto down = collision::moveVertically({{120.0f, 20.0f}, {48.0f, 48.0f}},
                                               1000.0f, arena, platforms);
    expect(down.blocked && near(down.y, 152.0f), "Fast fall lands on nearest thin platform");
    const auto up = collision::moveVertically({{120.0f, 400.0f}, {48.0f, 48.0f}},
                                             -1000.0f, arena, platforms);
    expect(up.blocked && near(up.y, 358.0f), "Fast ascent hits nearest platform underside");
    const auto ceiling = collision::moveVertically({{500.0f, 100.0f}, {48.0f, 48.0f}},
                                                  -1000.0f, arena, platforms);
    expect(ceiling.blocked && near(ceiling.y, 0.0f), "Arena ceiling blocks ascent");
    const auto bottom = collision::moveVertically({{500.0f, 100.0f}, {48.0f, 48.0f}},
                                                 1000.0f, arena, platforms);
    expect(bottom.blocked && near(bottom.y, 492.0f), "Arena bottom blocks falling");
    expect(!collision::isSupported({{300.0f, 152.0f}, {48.0f, 48.0f}}, arena, platforms),
           "Touching platform side does not count as support");

    World slow;
    World fast;
    slow.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
    fast.setPlayerInput(World::localPlayerId, PlayerInput{0.0f, true});
    for (int i = 0; i < 9; ++i) slow.update(1.0f / 30.0f);
    for (int i = 0; i < 36; ++i) fast.update(1.0f / 120.0f);
    expect(near(slow.players().front().position.y, fast.players().front().position.y) &&
               near(slow.players().front().velocity.y, fast.players().front().velocity.y),
           "Jump physics agree at 30 and 120 updates per second");
}

} // namespace

int main()
{
    try {
        testMovement();
        testTimeSteps();
        testCollisions();
        testInputAndWeaponState();
        testJump();
        testPlatform();
        testVerticalCollisions();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Game tests passed\n";
    return 0;
}
