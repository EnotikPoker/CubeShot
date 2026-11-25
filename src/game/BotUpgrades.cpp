#include "game/BotUpgrades.h"
#include "game/AttackStats.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
namespace
{
float score(WeaponKind weapon, const Abilities& a, const ActiveLoadout& active, const Player& enemy,
            float gap, const Arena& arena)
{
    auto d = weaponDefinition(weapon);
    auto stats = attackStats(weapon, a);
    // Include the delayed blast and the warmed-up beam, not just the projectile's
    // contact damage. Both otherwise look like weaker versions of the pistol.
    const float attackDamage = weapon == WeaponKind::StickyLauncher ? 60.f
                               : weapon == WeaponKind::Beam         ? 4.5f
                                                                    : float(d.damage);
    const float cycleFloor = weapon == WeaponKind::Beam ? .03f : .1f;
    float reach = meleeWeapon(weapon)                   ? stats.reach
                  : weapon == WeaponKind::FireBoomerang ? stats.boomRange
                  : d.effectiveRange > 0                ? d.effectiveRange * a.range()
                  : weapon == WeaponKind::Flamethrower  ? 350 * a.range()
                  : weapon == WeaponKind::Rpg           ? 650.f
                                                        : 470.f;
    float interval = d.interval / a.fireRate();
    Weapon magazine;
    magazine.magazineLevel = a.level(AbilityKind::Magazine);
    magazine.reloadLevel = a.level(AbilityKind::FastReload);
    magazine.equip(weapon);
    const float capacity = magazine.capacity();
    const float rounds = capacity * (a.level(AbilityKind::Economy) ? 4.f / 3 : 1.f);
    float uptime = rounds ? rounds * interval / (rounds * interval + magazine.reloadDuration()) : 1;
    float hit = meleeWeapon(weapon) ? 1.f : std::clamp(1 - d.spread * a.accuracy() * gap / 12000, .25f, 1.f);
    float approach =
        std::max(0.f, gap - reach) /
        (Player::moveSpeed * a.speed() + (meleeWeapon(weapon) ? 40 * a.level(AbilityKind::Lunge) : 0));
    approach /= 1 + .06f * a.level(AbilityKind::AirManeuver);
    float health = float(a.health()) + a.shields() * 25;
    health *= 1 + .10f * a.level(AbilityKind::Tempering);
    if (meleeWeapon(weapon))
        health *= 1 + .12f * a.level(AbilityKind::Stance);
    for (auto kind : active)
        switch (kind)
        {
            case ActiveKind::Dash:
            case ActiveKind::Blink:
                approach *= .55f;
                break;
            case ActiveKind::Harpoon:
                if (meleeWeapon(weapon) || weapon == WeaponKind::Flamethrower)
                    approach *= .45f;
                break;
            case ActiveKind::Barrier:
                health += 60;
                break;
            case ActiveKind::Heal:
                health += 35;
                break;
            default:
                break;
        }
    float enemyDps = weaponDefinition(enemy.weapon.kind).damage * enemy.abilities.power() /
                     std::max(.1f, weaponDefinition(enemy.weapon.kind).interval);
    bool enemyCloses = false;
    float enemyHealth = float(enemy.healthLimit()) + enemy.shields * 25;
    for (auto slot : enemy.actives)
    {
        enemyCloses = enemyCloses || slot.kind == ActiveKind::Blink || slot.kind == ActiveKind::Dash ||
                      slot.kind == ActiveKind::Harpoon;
        if (slot.kind == ActiveKind::Barrier)
            enemyHealth += 60;
        if (slot.kind == ActiveKind::Heal)
            enemyHealth += 30;
    }
    if (meleeWeapon(enemy.weapon.kind) && !meleeWeapon(weapon))
        enemyDps *= enemyCloses ? .65f : .35f;
    float safety = std::clamp(1 - approach * enemyDps / health * .25f, .2f, 1.f);
    float dps = attackDamage * d.pellets * a.power() * hit / std::max(cycleFloor, interval) * uptime;
    dps *= 1 + .04f * a.level(AbilityKind::Executioner);
    if (capacity && weapon != WeaponKind::Beam)
        dps *= 1 + .25f * a.level(AbilityKind::FreshMagazine) / capacity;
    if (active[0] != ActiveKind::None || active[1] != ActiveKind::None)
        dps *= 1 + .04f * a.level(AbilityKind::Resonance) + .015f * a.level(AbilityKind::Transfer);
    if (meleeWeapon(enemy.weapon.kind) && !meleeWeapon(weapon))
        health *= 1 + .06f * a.level(AbilityKind::Impulse);
    if (weapon == WeaponKind::FireBoomerang)
        dps *= 2 * (1 + .25f * a.level(AbilityKind::ReturnStrike)) *
               (1 + .65f * a.level(AbilityKind::SecondCircuit));
    if (meleeWeapon(weapon))
        dps *= 1 + .12f * a.level(AbilityKind::Combo) + .10f * a.level(AbilityKind::Bleeding) +
               .06f * a.level(AbilityKind::WideSwing) + .35f / 3 * a.level(AbilityKind::Echo);
    if (weapon == WeaponKind::Rpg || weapon == WeaponKind::GrenadeLauncher)
        dps *= gap < 200 ? .3f : 1.f;
    for (auto kind : active)
    {
        if (kind == ActiveKind::Wave)
            dps += gap < 300 ? 7 : 2;
        if (kind == ActiveKind::Slam)
            dps += gap < 300 ? 10 : 2;
        if (kind == ActiveKind::Overdrive)
            dps *= 1.12f;
        if (kind == ActiveKind::Parry || kind == ActiveKind::Cleanse || kind == ActiveKind::Screen)
            health += 15;
        if (kind == ActiveKind::Mine || kind == ActiveKind::IceGrenade)
            dps += 3;
        if (kind == ActiveKind::Bomb)
            dps += 6;
    }
    if (a.level(AbilityKind::Incendiary))
        dps += 4;
    if (a.level(AbilityKind::Vampirism))
        health *= 1.1f;
    // Exposure while closing matters more on a broad open arena.
    float covered = 0;
    for (auto r : arena.platforms)
        if (r.position.y < arena.bounds.size.y - 100 && r.size.x > 100)
            covered += .03f;
    float alpha = attackDamage * d.pellets * a.power();
    float hits = std::ceil(enemyHealth / std::max(1.f, alpha));
    float ttk = std::max(.3f, (hits - 1) * interval / std::max(.25f, hit) + d.preparation);
    float burst = enemyHealth / ttk;
    return (.6f * dps + .4f * burst) * safety * (health / 100) /
           (1 + approach * std::max(.08f, .2f - covered));
}
} // namespace

UpgradeKind chooseBotUpgrade(const WorldSnapshot& state, PlayerId own,
                             const std::deque<BotRoundStats>& history)
{
    if (own < 1 || own > 2 || state.players.size() != 2)
        return UpgradeKind::Continue;
    unsigned index = own - 1;
    auto enemy = state.players[1 - index];
    auto weapon = state.loadout[index];
    auto traits = state.abilities[index];
    auto active = state.activeLoadout[index];
    auto arena = Arena::create(state.map);
    float gap = std::hypot(arena.spawnPoints[0].x - arena.spawnPoints[1].x,
                           arena.spawnPoints[0].y - arena.spawnPoints[1].y);
    float elapsed = 0, inReach = 0, damage = 0, selfHarm = 0;
    unsigned shots = 0;
    for (auto r : history)
    {
        elapsed += r.seconds;
        inReach += r.inReach;
        damage += r.damage;
        selfHarm += r.selfDamage;
        shots += r.shots;
    }
    auto utility = [&](WeaponKind w, const Abilities& a, const ActiveLoadout& slots)
    { return score(w, a, slots, enemy, gap, arena); };
    float current = utility(weapon, traits, active), best = current;
    UpgradeKind choice = UpgradeKind::Continue;
    float failure = 1;
    if (shots >= 12 && damage / shots < weaponDefinition(weapon).damage * .15f)
        failure *= .65f;
    if (elapsed > 8 && meleeWeapon(weapon) && inReach / elapsed < .15f)
        failure *= .6f;
    if (selfHarm > 50)
        failure *= .7f;
    current *= failure;
    best = current;
    float expected = 0;
    for (unsigned w = 0; w < weaponCount; ++w)
        expected += utility(WeaponKind(w), traits, active) / weaponCount;
    if (state.rules.mode != GameMode::ArmsRace && expected > best * 1.03f)
    {
        best = expected;
        choice = UpgradeKind::Weapon;
    }
    auto pool = traits.available(weapon, active);
    if (!pool.empty())
    {
        expected = 0;
        for (auto k : pool)
        {
            auto next = traits;
            next.add(k);
            expected += utility(weapon, next, active) * failure / pool.size();
        }
        if (expected > best * 1.01f)
        {
            best = expected;
            choice = UpgradeKind::Ability;
        }
    }
    for (unsigned slot = 0; slot < 2; ++slot)
    {
        expected = 0;
        unsigned n = 0;
        for (unsigned k = 1; k <= activeCount; ++k)
            if (ActiveKind(k) != active[1 - slot])
            {
                auto next = active;
                next[slot] = ActiveKind(k);
                expected += utility(weapon, traits, next) * failure;
                ++n;
            }
        if (n && expected / n > best * 1.01f)
        {
            best = expected / n;
            choice = slot ? UpgradeKind::ActiveE : UpgradeKind::ActiveQ;
        }
    }
    return choice;
}
} // namespace cubeshot
