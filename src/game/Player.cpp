#include "game/Player.h"
#include "game/AttackStats.h"

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

namespace
{
struct BodyPose
{
    float x, y, shear;
};

BodyPose bodyPose(const Player& player)
{
    const float stride = player.grounded
                             ? std::sin(player.motionPhase) *
                                   std::min(1.f, std::abs(player.velocity.x) / player.movementSpeed())
                             : 0;
    const float rebound = -player.landing * .12f * std::cos((1 - player.landing) * 4.08407f);
    const float stretch = player.grounded
                              ? rebound + stride * .04f
                              : std::clamp(-player.velocity.y / player.jumpVelocity() * .09f, -.06f, .1f);
    return {1 - stretch * .65f, 1 + stretch, player.lean * .055f};
}
} // namespace

Vec2 Player::bodyPoint(Vec2 local) const
{
    const auto pose = bodyPose(*this);
    return {(24 + (local.x - 24) * pose.x + (48 - local.y) * pose.shear) * bodyScale(),
            (48 + (local.y - 48) * pose.y) * bodyScale()};
}

Vec2 Player::bodyLocal(Vec2 point) const
{
    point = {point.x / bodyScale(), point.y / bodyScale()};
    const auto pose = bodyPose(*this);
    const float y = 48 + (point.y - 48) / pose.y;
    return {24 + (point.x - 24 - (48 - y) * pose.shear) / pose.x, y};
}

Polygon Player::bodyContour() const
{
    auto shape = damageOutline(surface);
    for (auto& v : shape)
        v = bodyPoint(v);
    return shape;
}

bool Player::alive() const
{
    return health > 0;
}

Vec2 Player::weaponOrigin() const
{
    const auto local = bodyPoint({24, 26.4f});
    return {position.x + local.x, position.y + local.y};
}

Vec2 Player::muzzlePosition() const
{
    const Vec2 origin = weaponOrigin();
    return {origin.x + aimDirection.x * weapon.barrel(), origin.y + aimDirection.y * weapon.barrel()};
}

std::optional<Bullet> Player::takeShot()
{
    weapon.driven = triggerHeld_ && !weapon.reloading;
    bool requested = shootRequested_ || (weapon.automatic() && triggerHeld_);
    shootRequested_ = false;
    if (!alive())
        return {};
    const auto& definition = weaponDefinition(weapon.kind);
    if (weapon.reloading && weapon.kind != WeaponKind::Shotgun)
        return {};
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
    auto bullet = shotPrototype(shotAim_, shotSequence_);
    weapon.cooldown = std::max(.03f, weapon.cooldown / (abilities.fireRate() * (1 + .15f * adrenaline())));
    applyImpulse({-shotAim_.x * definition.recoil * abilities.recoil(),
                  -shotAim_.y * definition.recoil * abilities.recoil()});
    if (meleeWeapon(weapon.kind) && lungeCooldown <= 0 && abilities.level(AbilityKind::Lunge))
    {
        float force = 120 * abilities.level(AbilityKind::Lunge);
        applyImpulse({shotAim_.x * force, shotAim_.y * force});
        lungeCooldown = .5f;
    }
    return bullet;
}

Bullet Player::shotPrototype(Vec2 direction, std::uint64_t sequence) const
{
    const auto& definition = weaponDefinition(weapon.kind);
    Bullet bullet;
    bullet.position = weaponOrigin();
    bullet.velocity = {direction.x * weapon.bulletSpeed, direction.y * weapon.bulletSpeed};
    bullet.damage = weapon.damage;
    bullet.shooterId = id;
    bullet.shotSequence = sequence;
    bullet.weapon = weapon.kind;
    bullet.volley = weapon.shots;
    bullet.lifetime = definition.lifetime;
    bullet.acceleration = definition.gravity;
    bullet.collisionRadius =
        Bullet::radius *
        (abilities.level(AbilityKind::LargeBullets) && !meleeWeapon(weapon.kind) ? 1.5f : 1.f);
    bullet.ignites = abilities.level(AbilityKind::Incendiary) > 0;
    bullet.power =
        abilities.power() *
        (meleeWeapon(weapon.kind) && abilities.level(AbilityKind::Combo) ? 1 + .1f * comboHits : 1);
    bullet.terrainPower = abilities.demolition();
    bullet.blastScale = abilities.blastRadius();
    bullet.rangeScale = abilities.range();
    bullet.spreadScale = abilities.accuracy();
    if (ballisticWeapon(weapon.kind) || weapon.kind == WeaponKind::FireBoomerang)
    {
        bullet.twinned = abilities.level(AbilityKind::TwinShot) > 0;
        bullet.ricochets = abilities.level(AbilityKind::Ricochet);
        bullet.penetrations =
            abilities.level(AbilityKind::Penetration) + (weapon.kind == WeaponKind::Crossbow ? 1 : 0);
    }
    if (ballisticWeapon(weapon.kind) || explosiveWeapon(weapon.kind) ||
        weapon.kind == WeaponKind::FireBoomerang)
    {
        bullet.velocity.x *= abilities.projectileSpeed();
        bullet.velocity.y *= abilities.projectileSpeed();
    }
    if (weapon.kind == WeaponKind::Flamethrower)
        bullet.lifetime *= abilities.range();
    if (weapon.kind == WeaponKind::FireBoomerang)
    {
        auto stats = attackStats(weapon.kind, abilities);
        bullet.boomOutSpeed = stats.boomOut;
        bullet.boomReturnSpeed = stats.boomReturn;
        bullet.boomRange = stats.boomRange;
        bullet.returnPower = stats.returnPower;
        bullet.circuits = stats.circuits;
        bullet.launchDirection = direction;
        bullet.lifetime = 2.4f * abilities.range() * (1 + stats.circuits) / abilities.projectileSpeed();
    }
    return bullet;
}

void Player::setAbilities(const Abilities& value, bool refill)
{
    if (!value.valid())
        return;
    const float previous = size.x;
    abilities = value;
    size = {48 * bodyScale(), 48 * bodyScale()};
    position.x += (previous - size.x) / 2;
    position.y += previous - size.y;
    weapon.magazineLevel = abilities.level(AbilityKind::Magazine);
    weapon.reloadLevel = abilities.level(AbilityKind::FastReload);
    weapon.bodyScale = bodyScale();
    if (refill)
    {
        health = healthLimit();
        airJumps = 0;
        secondChanceUsed = false;
        invulnerability = 0;
        damageQuiet = 3;
        healingRemainder = 0;
        shields = abilities.shields();
        jetpackFuel = 2;
        weapon.ammo = weapon.capacity();
        weapon.cancelReload();
        blockedShots_.clear();
    }
    else
    {
        health = std::min(health, healthLimit());
        shields = std::min(shields, abilities.shields());
        weapon.ammo = std::min(weapon.ammo, weapon.capacity());
    }
}

bool Player::absorb(const Bullet& b)
{
    if (b.shooterId == id || b.shooterId == 0)
        return false;
    for (auto shot : blockedShots_)
        if (shot.shooter == b.shooterId && shot.volley == b.volley)
            return true;
    if (!shields)
        return false;
    --shields;
    if (blockedShots_.size() >= 512)
        blockedShots_.erase(blockedShots_.begin());
    blockedShots_.push_back({b.shooterId, b.volley});
    return true;
}

void Player::applyImpulse(Vec2 impulse)
{
    if (!alive())
        return;
    const float old = pushX;
    pushX = std::clamp(pushX + impulse.x, -900.f, 900.f);
    velocity.x += pushX - old;
    velocity.y = std::max(-1100.f, velocity.y + impulse.y);
    if (velocity.y < 0)
        grounded = false;
}

void Player::heal(float amount)
{
    if (!alive() || amount <= 0)
        return;
    healingRemainder += amount;
    int whole = static_cast<int>(healingRemainder);
    healingRemainder -= whole;
    health = std::min(healthLimit(), health + whole);
}

void Player::takeDamage(int damage)
{
    health = std::max(0, std::clamp(health, 0, healthLimit()) - std::max(0, damage));
    if (damage > 0)
        damageQuiet = 0;
    if (!alive() && abilities.level(AbilityKind::SecondChance) && !secondChanceUsed)
    {
        secondChanceUsed = true;
        health = std::max(1, healthLimit() / 4);
        burning = 0;
        burnTick = 0;
        invulnerability = .5f;
        surface = {};
    }
    if (!alive())
    {
        velocity = {};
        pushX = 0;
        weapon.cancelReload();
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
    if (input.activeQ && !activeHeldQ_)
        activeRequests |= 1;
    if (input.activeE && !activeHeldE_)
        activeRequests |= 2;
    activeHeldQ_ = input.activeQ;
    activeHeldE_ = input.activeE;
    velocity.x = std::clamp(input.moveDirection, -1.0f, 1.0f) * movementSpeed() + pushX;
    jetpackHeld_ = input.jetpackHeld || input.jump;
    if (input.reload)
        weapon.startReload();
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

void Player::requestAction(bool jump, bool shoot, Vec2 aim, std::uint64_t sequence, bool reload,
                           unsigned activeMask)
{
    if (!alive())
        return;
    activeRequests |= activeMask & 3;
    if (activeMask)
        activeSequence = sequence;
    if (activeMask)
    {
        float n = std::hypot(aim.x, aim.y);
        if (n > .001f)
            aimDirection = {aim.x / n, aim.y / n};
    }
    if (reload)
        weapon.startReload();
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
    activeRequests = 0;
    activeHeldQ_ = activeHeldE_ = false;
    velocity.x = pushX;
    jetpackHeld_ = false;
    jetpackActive = false;
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

    for (auto& slot : actives)
        slot.cooldown = std::max(0.f, slot.cooldown - deltaSeconds);
    barrierTime = std::max(0.f, barrierTime - deltaSeconds);
    if (barrierTime == 0)
        barrier = 0;
    lungeCooldown = std::max(0.f, lungeCooldown - deltaSeconds);
    comboTime = std::max(0.f, comboTime - deltaSeconds);
    if (comboTime == 0)
    {
        comboHits = 0;
        comboTarget = 0;
    }
    slamTime = std::max(0.f, slamTime - deltaSeconds);
    if (slamTime == 0)
        slamming = false;
    if (slamming)
        velocity.y = fallSpeed;
    const bool dash = dashTime > 0;
    if (dash)
    {
        float ratio = std::min(dashTime, deltaSeconds) / deltaSeconds;
        velocity = {dashDirection.x * 1250 * ratio + pushX, dashDirection.y * 1250 * ratio};
        dashTime = std::max(0.f, dashTime - deltaSeconds);
    }
    grounded = playerSupport || collision::isSupported(bounds(), arena.bounds, arena.platforms);
    invulnerability = std::max(0.f, invulnerability - deltaSeconds);
    damageQuiet += deltaSeconds;
    if (damageQuiet >= 3)
        heal(4 * abilities.level(AbilityKind::Regeneration) * deltaSeconds);
    if (grounded)
    {
        jetpackFuel = 2;
        airJumps = 0;
    }
    if (jumpRequested_ && (grounded || (abilities.level(AbilityKind::DoubleJump) && airJumps < 1)))
    {
        if (!grounded)
            ++airJumps;
        velocity.y = -jumpVelocity();
        grounded = false;
    }
    jumpRequested_ = false;

    jetpackActive = false;
    if (grounded)
        jetpackFuel = 2;
    // Короткие шаги сохраняют дугу прыжка и столкновения при редких обновлениях.
    double remaining = deltaSeconds;
    constexpr double maxStep = 1.0 / 120.0;
    while (remaining > 0.0)
    {
        const float step = static_cast<float>(std::min(remaining, maxStep));
        remaining -= std::min(remaining, maxStep);
        const float oldPush = pushX;
        pushX *= std::exp(-(grounded ? 10.f : 3.f) * step);
        velocity.x += pushX - oldPush;
        const float oldX = position.x;
        const bool wasGrounded = grounded;
        const float falling = velocity.y;
        std::vector<Rect> nearby;
        const auto* solids = &arena.platforms;
        if (arena.platforms.size() > 32)
        {
            nearby = arena.solidsNear({{position.x - std::abs(velocity.x * step) - 2,
                                        position.y - std::abs(velocity.y * step) - 24},
                                       {size.x + 2 * std::abs(velocity.x * step) + 4,
                                        size.y + 2 * std::abs(velocity.y * step) + 48}});
            solids = &nearby;
        }
        position.x = collision::moveHorizontally(bounds(), velocity.x * step, arena.bounds, *solids);
        const bool thrust =
            abilities.level(AbilityKind::Jetpack) && jetpackHeld_ && !grounded && jetpackFuel > 0;
        if (thrust && !dash)
        {
            float burn = std::min(step, jetpackFuel);
            velocity.y = std::max(-1100.f, velocity.y - 2600 * burn);
            jetpackFuel = std::max(0.f, jetpackFuel - burn);
            jetpackActive = true;
        }
        if (!dash)
        {
            velocity.y += gravity * step;
            velocity.y =
                std::min(velocity.y, fallSpeed * (abilities.level(AbilityKind::FeatherFall) ? .6f : 1.f));
        }
        const auto movement = collision::moveVertically(bounds(), velocity.y * step, arena.bounds, *solids);
        position.y = movement.y;
        grounded = movement.blocked && velocity.y > 0.0f;
        if (grounded)
        {
            jetpackFuel = 2;
            airJumps = 0;
        }
        if (movement.blocked)
        {
            velocity.y = 0.0f;
            if (!wasGrounded && falling > 90)
                landing = std::min(1.f, falling / 600);
        }
        if (grounded)
            motionPhase = std::remainder(motionPhase + std::abs(position.x - oldX) * .16f, 6.2831853f);
        landing = std::max(0.f, landing - step * 6);
        lean += (std::clamp(velocity.x / movementSpeed(), -1.f, 1.f) - lean) * std::min(1.f, step * 14);
    }
    if (dash)
    {
        velocity.x = pushX;
        velocity.y = 0;
    }
    weapon.update(deltaSeconds);
}

} // namespace cubeshot

namespace cubeshot
{
void Player::setActives(const ActiveLoadout& loadout)
{
    if (!validActiveLoadout(loadout))
        return;
    for (unsigned i = 0; i < 2; ++i)
        actives[i] = {loadout[i], 0};
    activeRequests = 0;
    dashTime = barrier = barrierTime = 0;
    slamming = false;
}
} // namespace cubeshot
