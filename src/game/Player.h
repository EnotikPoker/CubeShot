#pragma once

#include "game/Bullet.h"
#include "game/PlayerInput.h"
#include "game/Types.h"
#include "game/Weapon.h"

#include <optional>

namespace cubeshot
{

struct Arena;

struct Player
{
    static constexpr int maxHealth = 100;
    static constexpr float defaultSize = 48.0f;
    static constexpr float moveSpeed = 280.0f; // Пикселей в секунду.
    static constexpr float jumpSpeed = 620.0f; // Начальная скорость вверх, пикселей/с.
    static constexpr float gravity = 1800.0f;  // Пикселей/с².

    PlayerId id = 0;
    Vec2 position;
    Vec2 velocity;
    Vec2 size{defaultSize, defaultSize};
    int health = maxHealth;
    Weapon weapon;
    bool grounded = false;
    Vec2 aimDirection{1.0f, 0.0f};

    Rect bounds() const;
    bool alive() const;
    Vec2 weaponOrigin() const;
    Vec2 muzzlePosition() const;
    std::optional<Bullet> takeShot();
    void takeDamage(int damage);
    void applyInput(const PlayerInput& input);
    void requestAction(bool jump, bool shoot, Vec2 aim, std::uint64_t sequence = 0);
    void clearInput();
    void update(float deltaSeconds, const Arena& arena, bool playerSupport = false);

private:
    bool jumpHeld_ = false;
    bool jumpRequested_ = false;
    bool shootHeld_ = false;
    bool shootRequested_ = false;
    Vec2 shotAim_{1, 0};
    std::uint64_t shotSequence_ = 0;
};

} // namespace cubeshot
