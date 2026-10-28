#include "game/Player.h"

#include "game/Arena.h"
#include "game/Collision.h"

#include <algorithm>
#include <cmath>

namespace cubeshot
{

Rect Player::bounds() const
{
    return {position, size};
}

bool Player::alive() const
{
    return health > 0;
}

Vec2 Player::weaponOrigin() const
{
    return {position.x + size.x * 0.5f, position.y + size.y * 0.55f};
}

Vec2 Player::muzzlePosition() const
{
    const Vec2 origin = weaponOrigin();
    return {origin.x + aimDirection.x * weapon.barrel(), origin.y + aimDirection.y * weapon.barrel()};
}

std::optional<Bullet> Player::takeShot()
{
    weapon.driven = triggerHeld_;
    bool requested = shootRequested_ || (weapon.automatic() && triggerHeld_);
    shootRequested_ = false;
    if (!alive())
        return {};
    const auto& definition = weaponDefinition(weapon.kind);
    if (weapon.charging)
    {
        if (!triggerHeld_)
        {
            weapon.charging = false;
            weapon.preparation = 0;
            return {};
        }
        if (weapon.preparation > 0)
            return {};
        weapon.charging = false;
        requested = true;
        shotAim_ = aimDirection;
    }
    else if (requested && definition.preparation > 0 && weapon.cooldown <= 0)
    {
        if (triggerHeld_)
        {
            weapon.charging = true;
            weapon.preparation = definition.preparation;
        }
        return {};
    }
    if (!requested || !weapon.tryFire())
        return {};
    if (weapon.automatic())
        shotAim_ = aimDirection;
    // World проверит путь от руки до дула, чтобы не стрелять сквозь стену.
    Bullet bullet;
    bullet.position = weaponOrigin();
    bullet.velocity = {shotAim_.x * weapon.bulletSpeed, shotAim_.y * weapon.bulletSpeed};
    bullet.damage = weapon.damage;
    bullet.shooterId = id;
    bullet.shotSequence = shotSequence_;
    bullet.weapon = weapon.kind;
    bullet.volley = weapon.shots;
    bullet.lifetime = definition.lifetime;
    bullet.acceleration = definition.gravity;
    return bullet;
}

void Player::takeDamage(int damage)
{
    health = std::max(0, std::clamp(health, 0, maxHealth) - std::max(0, damage));
    if (!alive())
    {
        velocity = {};
        jumpRequested_ = false;
        shootRequested_ = false;
    }
}

void Player::applyInput(const PlayerInput& input)
{
    if (!alive())
    {
        return;
    }
    velocity.x = std::clamp(input.moveDirection, -1.0f, 1.0f) * moveSpeed;
    jumpRequested_ = jumpRequested_ || (input.jump && !jumpHeld_);
    jumpHeld_ = input.jump;
    const bool shot = input.shoot && !shootHeld_ && !shootRequested_;
    shootRequested_ = shootRequested_ || shot;
    shootHeld_ = input.shoot;
    triggerHeld_ = input.triggerHeld || input.shoot;
    const float length = std::hypot(input.aimDirection.x, input.aimDirection.y);
    if (std::isfinite(length) && length > 0.0001f)
    {
        aimDirection = {input.aimDirection.x / length, input.aimDirection.y / length};
    }
    if (shot)
        shotAim_ = aimDirection;
}

void Player::requestAction(bool jump, bool shoot, Vec2 aim, std::uint64_t sequence)
{
    if (!alive())
        return;
    jumpRequested_ = jumpRequested_ || jump;
    if (shoot && !shootRequested_)
    {
        const float length = std::hypot(aim.x, aim.y);
        if (!std::isfinite(length) || length < 0.0001f)
            return;
        shootRequested_ = true;
        shotSequence_ = sequence;
        shotAim_ = {aim.x / length, aim.y / length};
    }
}

void Player::clearInput()
{
    velocity.x = 0;
    weapon.driven = false;
    jumpHeld_ = jumpRequested_ = false;
    shootHeld_ = shootRequested_ = triggerHeld_ = false;
    weapon.charging = false;
    weapon.preparation = 0;
}

void Player::update(float deltaSeconds, const Arena& arena, bool playerSupport)
{
    if (!alive() || !(deltaSeconds > 0.0f))
    {
        return;
    }

    grounded = playerSupport || collision::isSupported(bounds(), arena.bounds, arena.platforms);
    if (jumpRequested_ && grounded)
    {
        velocity.y = -jumpSpeed;
        grounded = false;
    }
    jumpRequested_ = false;

    // Короткие шаги сохраняют дугу прыжка и столкновения при редких обновлениях.
    double remaining = deltaSeconds;
    constexpr double maxStep = 1.0 / 120.0;
    while (remaining > 0.0)
    {
        const float step = static_cast<float>(std::min(remaining, maxStep));
        remaining -= std::min(remaining, maxStep);
        position.x = collision::moveHorizontally(bounds(), velocity.x * step, arena.bounds, arena.platforms);
        velocity.y += gravity * step;
        const auto movement =
            collision::moveVertically(bounds(), velocity.y * step, arena.bounds, arena.platforms);
        position.y = movement.y;
        grounded = movement.blocked && velocity.y > 0.0f;
        if (movement.blocked)
        {
            velocity.y = 0.0f;
        }
    }
    weapon.update(deltaSeconds);
}

} // namespace cubeshot
