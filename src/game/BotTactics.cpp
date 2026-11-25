#include "game/AttackQueries.h"
#include "game/BodyPhysics.h"
#include "game/BotController.h"
#include "game/BotQueries.h"
#include "game/Collision.h"
#include "game/ProjectileMotion.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cubeshot
{
using namespace botdetail;

float BotController::hazardCost(Vec2 position) const
{
    float cost = 0;
    for (const auto& fire : perceived_.fires)
    {
        const float dx =
            std::max(0.f, std::abs(position.x + 24 * movementAbilities_.value_or(Abilities{}).scale() -
                                   fire.position.x) -
                              24 * movementAbilities_.value_or(Abilities{}).scale());
        const float dy =
            std::max(0.f, std::abs(position.y + 24 * movementAbilities_.value_or(Abilities{}).scale() -
                                   fire.position.y) -
                              24 * movementAbilities_.value_or(Abilities{}).scale());
        const float distance = std::hypot(dx, dy);
        if (distance < fire.radius + 12)
            cost += 1.f + (fire.radius + 12 - distance) / 20;
    }
    auto abilities = movementAbilities_.value_or(Abilities{});
    return cost * (abilities.level(AbilityKind::FireResistance) ? .5f : 1.f) *
           (1 - .1f * abilities.level(AbilityKind::ToughSkin));
}

Vec2 BotController::chooseGoal(const Player& self, const std::optional<BotTarget>& target)
{
    if (!target)
        return self.position;
    auto profile = abilityProfile(self.weapon.kind, self.abilities);
    const auto enemy = abilityProfile(target->weapon, target->abilities);
    const bool currentAttack = botAim(self, *target, arena_, 0, false).has_value();
    const bool close = meleeWeapon(self.weapon.kind) || self.weapon.kind == WeaponKind::Shotgun ||
                       self.weapon.kind == WeaponKind::Flamethrower;
    const bool threatMelee = meleeWeapon(target->weapon);
    auto ready = [&](ActiveKind kind)
    {
        return std::any_of(target->actives.begin(), target->actives.end(),
                           [&](auto s) { return s.kind == kind && s.cooldown <= 0; });
    };
    const bool gapCloser = ready(ActiveKind::Blink) || ready(ActiveKind::Dash) || ready(ActiveKind::Harpoon);
    const float dangerReach = enemy.maximum + (threatMelee && gapCloser ? 180.f : 0.f);
    if (threatMelee && !meleeWeapon(self.weapon.kind))
        profile.minimum = std::max(profile.minimum, std::min(profile.maximum - 20, dangerReach + 85));
    const float enemyInterval = weaponDefinition(target->weapon).interval / target->abilities.fireRate();
    const bool enemyReady = !target->reloading && !target->overheated &&
                            (time_ - enemyShotAt_ > enemyInterval || target->weapon != enemyWeapon_);
    // Use an actual range advantage against short-range firearms as well as melee.
    // During an observed overheat/recovery window it is safe to keep closing.
    if (enemyReady && currentAttack && !threatMelee && !meleeWeapon(self.weapon.kind) &&
        profile.maximum > enemy.maximum + 60)
        profile.minimum = std::max(profile.minimum, std::min(enemy.maximum + 45, profile.maximum - 20));
    // When long shots repeatedly miss, seek a closer firing position within the
    // same weapon's envelope rather than circling at its maximum range forever.
    if (time_ - enemyProgressAt_ > 3)
        profile.maximum = std::min(profile.maximum, profile.minimum + 60);
    const bool lethal =
        (target->weapon == WeaponKind::Sniper || target->weapon == WeaponKind::Rifle) && self.shields == 0 &&
        self.health + self.barrier <= weaponDefinition(target->weapon).damage * target->abilities.power() *
                                          (1 - .1f * self.abilities.level(AbilityKind::ToughSkin));
    bool covered = false;
    for (const auto& wall : arena_.platforms)
        if (collision::segmentHit(center(*target), self.weaponOrigin(), wall, 3))
        {
            covered = true;
            break;
        }
    if (covered)
    {
        if (coverSince_ < 0)
            coverSince_ = time_;
        if (time_ - coverSince_ > (hard_ ? .35f : .7f) && self.weapon.cooldown <= 0 &&
            !self.weapon.overheated && !target->charging)
        {
            attackUntil_ = time_ + .9f;
            coverSince_ = -1;
        }
    }
    else
        coverSince_ = -1;
    const bool wantsCover =
        ((lethal && enemyReady) || self.weapon.overheated || self.weapon.reloading ||
         (self.abilities.level(AbilityKind::Regeneration) && self.health < self.healthLimit() / 2 &&
          !self.abilities.level(AbilityKind::Vampirism))) &&
        time_ >= attackUntil_;
    auto exposure = [&](Vec2 at)
    {
        const auto start = center(*target);
        const Vec2 finish{at.x + self.size.x / 2, at.y + self.size.y / 2};
        for (const auto& box : arena_.platforms)
            if (collision::segmentHit(start, finish, box, 3))
                return 0.f;
        const float distanceToEnemy = distance(start, finish);
        if (target->slamming && finish.y > start.y &&
            std::abs(finish.x - start.x) < 110 * target->abilities.blastRadius())
            return 700.f;
        if (target->charging && enemyReady)
            return 950.f;
        if (wantsCover)
            return (close ? 400.f : 240.f);
        if (threatMelee && !meleeWeapon(self.weapon.kind) && distanceToEnemy < dangerReach + 60)
            return 650.f;
        return 0.f;
    };
    auto baseCost = [&](Vec2 at)
    {
        const float range = distance({at.x + self.size.x / 2, at.y + self.size.y / 2}, center(*target));
        float out = std::max({profile.minimum - range, range - profile.maximum, 0.f});
        out *= threatMelee && !meleeWeapon(self.weapon.kind) ? 3.f : 1.3f;
        // A close-range fighter must keep closing during the sniper's recovery window.
        if (close && lethal && !enemyReady)
            out += std::max(0.f, range - profile.maximum) * .35f;
        return out + distance(at, self.position) * .18f + hazardCost(at) * 700 + exposure(at);
    };
    const float currentRange = distance(self.weaponOrigin(), center(*target));
    if (meleeWeapon(self.weapon.kind) && hazardCost(self.position) == 0 && currentAttack)
        return self.position;
    if (time_ >= repositionUntil_ && currentRange >= profile.minimum && currentRange <= profile.maximum &&
        hazardCost(self.position) == 0 && exposure(self.position) == 0 && currentAttack)
        return self.position;

    struct Candidate
    {
        float cost;
        std::size_t index;
    };

    const auto& nodes = navigation_->nodes();
    std::vector<Candidate> candidates;
    candidates.reserve(nodes.size());
    routeHazards_.clear();
    routeHazards_.reserve(nodes.size());
    for (std::size_t n = 0; n < nodes.size(); ++n)
    {
        candidates.push_back({baseCost(nodes[n].position), n});
        routeHazards_.push_back(hazardCost(nodes[n].position) * 8);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](auto a, auto b) { return a.cost < b.cost; });
    float best = std::numeric_limits<float>::max();
    Vec2 goal = self.position;
    for (const auto& item : candidates)
    {
        if (item.cost > best)
            break;
        Player candidate = self;
        candidate.position = nodes[item.index].position;
        bool canAttack = false;
        if (candidate.weapon.kind == WeaponKind::Rpg)
        {
            const auto origin = candidate.weaponOrigin(), at = center(*target);
            const float flight =
                std::max(.01f, (distance(origin, at) - candidate.weapon.barrel()) / (shotSpeed(self)));
            const float angle = std::atan2(at.y - origin.y - 12.5f * flight * flight, at.x - origin.x);
            canAttack = flight <= weaponDefinition(WeaponKind::Rpg).lifetime &&
                        clearShot(candidate, {std::cos(angle), std::sin(angle)}, flight, arena_);
        }
        else if (candidate.weapon.kind == WeaponKind::GrenadeLauncher)
            canAttack = distance(candidate.position, target->position) > 200 &&
                        distance(candidate.position, target->position) < 500;
        else
            canAttack = botAim(candidate, *target, arena_, 0, false).has_value();
        float cost = item.cost + (canAttack ? 0.f : wantsCover ? 140.f : 450.f);
        if (time_ < repositionUntil_ && distance(nodes[item.index].position, goal_) < 40)
            cost += 300;
        if (self.abilities.level(AbilityKind::Regeneration) && self.health < self.healthLimit() / 2 && !close)
            cost += exposure(nodes[item.index].position) * .5f;
        if (cost >= best)
            continue;
        if (navigation_->nearest(self.position) != item.index &&
            navigation_->route(self.position, candidate.position, -1, -1, routeHazards_).empty())
        {
            const bool boost =
                mobile_ && (self.weapon.kind == WeaponKind::Shotgun ||
                            self.weapon.kind == WeaponKind::Minigun || self.weapon.kind == WeaponKind::Rpg);
            float rise = self.weapon.kind == WeaponKind::Minigun   ? 650.f
                         : self.weapon.kind == WeaponKind::Shotgun ? 320.f
                                                                   : 230.f;
            if (!boost || candidate.position.y < self.position.y - rise ||
                distance(candidate.position, self.position) > 800)
                continue;
        }
        best = cost;
        goal = candidate.position;
    }
    return goal;
}

} // namespace cubeshot
