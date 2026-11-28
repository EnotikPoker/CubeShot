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
    for (unsigned i = 0; i < rules_.participants; ++i)
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
    for (unsigned i = 0; i < rules_.participants; ++i)
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
                const Player* nearest = nullptr;
                float distance = 1e20f;
                for (const auto& candidate : world_.players())
                    if (candidate.id != i + 1 && candidate.alive())
                    {
                        if (collision::overlaps(probe.bounds(), candidate.bounds()))
                            safe = false;
                        if (side(candidate.id - 1) != side(i))
                        {
                            float d = std::hypot(pos.x - candidate.position.x, pos.y - candidate.position.y);
                            if (d < distance)
                            {
                                nearest = &candidate;
                                distance = d;
                            }
                        }
                    }
                const auto& enemy = nearest ? *nearest : world_.players()[i];
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
    std::array<bool, maxPlayers> inside{};
    for (unsigned i = 0; i < rules_.participants; ++i)
    {
        const auto& p = world_.players()[i];
        if (p.alive() && p.spawnProtection <= 0 &&
            std::hypot(p.position.x + p.size.x / 2 - at.x, p.position.y + p.size.y / 2 - at.y) <= 72)
            inside[side(i)] = true;
    }
    if (std::count(inside.begin(), inside.end(), true) == 1)
    {
        unsigned winner = unsigned(std::find(inside.begin(), inside.end(), true) - inside.begin());
        mode_.control[winner] += dt;
        unsigned points = std::min(rules_.controlGoal, unsigned(mode_.control[winner] + .0001f));
        if (rules_.format == MatchFormat::Teams)
            teamScore_[winner] = points;
        else
            score_[winner] = points;
    }
    double best = *std::max_element(mode_.control.begin(), mode_.control.end());
    bool end = best >= rules_.controlGoal || mode_.elapsed >= 480;
    if (end)
    {
        unsigned index =
            unsigned(std::max_element(mode_.control.begin(), mode_.control.end()) - mode_.control.begin());
        bool tie = std::count_if(mode_.control.begin(), mode_.control.end(),
                                 [&](double x) { return std::abs(x - best) < .001; }) > 1;
        winner_ = winnerTeam_ = 0;
        if (!tie)
        {
            if (rules_.format == MatchFormat::Teams)
                winnerTeam_ = index + 1;
            else
                winner_ = index + 1;
        }
        phase_ = MatchPhase::Finished;
        for (PlayerId id = 1; id <= rules_.participants; ++id)
            world_.clearPlayerInput(id);
        ++selectionRevision_;
    }
}
} // namespace cubeshot
