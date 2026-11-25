#include "game/Collision.h"
#include "game/Match.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
void Match::prepareChaos()
{
    unsigned old = unsigned(mode_.chaos), next = old;
    while (next == old || !next)
        next = std::uniform_int_distribution<unsigned>(1, 6)(random_);
    mode_.nextChaos = ChaosKind(next);
}

void Match::updateControl(float dt)
{
    mode_.elapsed = std::min(480.0, mode_.elapsed + double(dt));
    mode_.zoneRemaining -= dt;
    if (mode_.zoneRemaining <= 0)
    {
        mode_.zone = (mode_.zone + 1) % 3;
        mode_.zoneRemaining += 30;
    }
    world_.advanceDeadCooldowns(dt);
    world_.limitContinuousDebris(dt);
    auto before = world_.players();
    world_.update(dt);
    collectEvents();
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto& p = world_.players()[i];
        auto& r = mode_.respawn[i];
        if (before[i].alive() && !p.alive())
        {
            r.life = p.lifeRevision + 1;
            r.remaining = 3;
            r.decisionTime = 10;
            r.choosing = p.lastHostileAgo <= 8;
            weaponChoice_[i] = r.choosing ? WeaponChoice::Pending : WeaponChoice::Ready;
            spinRemaining_[i] = 0;
            upgrade_[i] = UpgradeKind::Continue;
            rolled_[i] = loadout_[i];
            rolledAbility_[i] = AbilityKind::None;
            rolledActive_[i] = ActiveKind::None;
            world_.clearPlayerInput(i + 1);
        }
        if (!p.alive() && !before[i].alive())
        {
            r.remaining = std::max(0.f, r.remaining - dt);
            r.decisionTime = std::max(0.f, r.decisionTime - dt);
            if (r.choosing && r.decisionTime == 0 && weaponChoice_[i] == WeaponChoice::Pending)
                weaponChoice_[i] = WeaponChoice::Ready;
        }
    }
    updateChoices(dt);
    for (unsigned i = 0; i < 2; ++i)
        if (!world_.players()[i].alive() && mode_.respawn[i].remaining == 0 &&
            weaponChoice_[i] == WeaponChoice::Ready)
        {
            auto places = world_.arena().respawnPoints();
            Vec2 best = world_.arena().spawnPoints[i];
            float utility = -1e9f;
            for (auto pos : places)
            {
                Player probe;
                probe.position = pos;
                probe.setAbilities(abilities_[i], false);
                bool safe = true;
                for (auto wall : world_.arena().platforms)
                    if (collision::overlaps(probe.bounds(), wall))
                        safe = false;
                const auto& enemy = world_.players()[1 - i];
                if (enemy.alive() && collision::overlaps(probe.bounds(), enemy.bounds()))
                    safe = false;
                if (!safe)
                    continue;
                float score = std::hypot(pos.x - enemy.position.x, pos.y - enemy.position.y);
                for (auto h : world_.arena().hazards)
                    if (std::abs(pos.x - h.bounds.position.x) < 100)
                        score -= 1000;
                for (auto fire : world_.fires())
                    if (std::hypot(pos.x - fire.position.x, pos.y - fire.position.y) < 100)
                        score -= 1000;
                for (auto entity : world_.entities())
                    if (entity.kind != EntityKind::Screen &&
                        std::hypot(pos.x - entity.position.x, pos.y - entity.position.y) < 160)
                        score -= 500;
                if (score > utility)
                {
                    utility = score;
                    best = probe.position;
                }
            }
            auto previous = world_.players()[i].actives;
            if (upgrade_[i] == UpgradeKind::ActiveQ)
                previous[0].cooldown = 0;
            if (upgrade_[i] == UpgradeKind::ActiveE)
                previous[1].cooldown = 0;
            world_.respawnPlayer(i + 1, best, loadout_[i], abilities_[i], activeLoadout_[i], previous,
                                 mode_.respawn[i].life);
            mode_.respawn[i].choosing = false;
        }
    auto anchors = world_.arena().controlAnchors();
    auto at = anchors[mode_.zone];
    bool inside[2]{};
    for (unsigned i = 0; i < 2; ++i)
    {
        auto p = world_.players()[i];
        inside[i] = p.alive() && p.spawnProtection <= 0 &&
                    std::hypot(p.position.x + p.size.x / 2 - at.x, p.position.y + p.size.y / 2 - at.y) <= 72;
    }
    for (unsigned i = 0; i < 2; ++i)
        if (inside[i] && !inside[1 - i])
        {
            mode_.control[i] += dt;
            score_[i] = std::min(rules_.controlGoal, unsigned(mode_.control[i] + .0001f));
        }
    bool end = mode_.control[0] >= rules_.controlGoal || mode_.control[1] >= rules_.controlGoal ||
               mode_.elapsed >= 480;
    if (end)
    {
        winner_ = std::abs(mode_.control[0] - mode_.control[1]) < .001f ? 0
                  : mode_.control[0] > mode_.control[1]                 ? 1
                                                                        : 2;
        phase_ = MatchPhase::Finished;
        for (PlayerId id : {1u, 2u})
            world_.clearPlayerInput(id);
        ++selectionRevision_;
    }
}
} // namespace cubeshot
