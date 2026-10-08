#include "game/Collision.h"
#include "game/World.h"

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
    reservedInput.jump = true;
    reservedInput.shoot = true;
    World world;
    world.setPlayerInput(World::localPlayerId, reservedInput);
    const float startY = world.players().front().position.y;
    world.update(0.1f);
    expect(world.bullets().empty() && near(world.players().front().position.y, startY),
           "Jumping and shooting remain unimplemented");

    Weapon weapon;
    weapon.cooldown = 0.25f;
    weapon.update(0.1f);
    expect(near(weapon.cooldown, 0.15f), "Weapon timer decreases in seconds");
    weapon.update(1.0f);
    expect(near(weapon.cooldown, 0.0f), "Weapon timer cannot become negative");
}

} // namespace

int main()
{
    try {
        testMovement();
        testTimeSteps();
        testCollisions();
        testInputAndWeaponState();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Game tests passed\n";
    return 0;
}
