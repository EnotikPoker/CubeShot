#include "game/BotRoster.h"
#include <algorithm>
#include <cmath>

namespace cubeshot
{
void BotRoster::reset()
{
    for (auto& a : actors_)
        a = Actor{};
}

void BotRoster::step(Match& match, float dt)
{
    auto state = match.snapshot();
    for (unsigned i = 0; i < state.rules.participants; ++i)
    {
        auto& a = actors_[i];
        if (state.slots[i] != SlotKind::Bot)
        {
            a = Actor{};
            continue;
        }
        const auto& self = match.world().players()[i];
        if (!a.controller || a.difficulty != state.botDifficulty[i])
        {
            a = Actor{};
            a.difficulty = state.botDifficulty[i];
            a.controller = std::make_unique<BotController>(match.world().arena(), BotDifficulty(a.difficulty),
                                                           seed_ + i * 7919);
        }
        if (a.observed && a.round == state.round && a.life == self.lifeRevision)
        {
            for (const auto& p : state.players)
            {
                int lost = std::max(0, a.health[p.id - 1] - p.health);
                if (p.id == self.id)
                {
                    a.stats.taken += lost;
                    if (p.lastDamageActor == self.id)
                        a.stats.selfDamage += lost;
                }
                else if (!match.world().allied(p.id, self.id) && p.lastDamageActor == self.id)
                    a.stats.damage += lost;
            }
            if (self.weapon.shots >= a.shots)
                a.stats.shots += self.weapon.shots - a.shots;
            if ((state.phase == MatchPhase::RoundEnd || !self.alive()) && !a.finishedHistory &&
                a.stats.seconds > 0)
            {
                a.history.push_back(a.stats);
                if (a.history.size() > 3)
                    a.history.pop_front();
                a.stats = {};
                a.finishedHistory = true;
            }
        }
        for (const auto& p : state.players)
            a.health[p.id - 1] = p.health;
        a.shots = self.weapon.shots;
        a.observed = true;
        if (a.round != state.round || a.life != self.lifeRevision)
        {
            if (a.stats.seconds > 0)
            {
                a.history.push_back(a.stats);
                if (a.history.size() > 3)
                    a.history.pop_front();
                a.stats = {};
            }
            a.controller->reset();
            a.controller->prepare(self.abilities, &match.world().arena());
            a.round = state.round;
            a.life = self.lifeRevision;
            a.finishedHistory = false;
            a.held = false;
            a.target = 0;
            a.targetTime = 0;
        }
        if ((state.phase == MatchPhase::Armory || state.mode.respawn[i].choosing) &&
            state.weaponChoice[i] == WeaponChoice::Pending)
        {
            auto kind = chooseBotUpgrade(state, i + 1, a.history);
            if (!match.chooseUpgrade(i + 1, kind, state.round, state.selectionRevision,
                                     state.mode.respawn[i].life))
                match.chooseWeapon(i + 1, false, state.round, state.selectionRevision);
        }
        if (state.phase != MatchPhase::Round || !self.alive())
            continue;
        a.targetTime = std::max(0.f, a.targetTime - dt);
        PlayerId target = 0;
        float best = -1e20f;
        for (const auto& p : match.world().players())
        {
            if (!p.alive() || p.id == self.id || match.world().allied(p.id, self.id))
                continue;
            float gap = std::hypot(p.position.x - self.position.x, p.position.y - self.position.y);
            auto range = botWeaponProfile(self.weapon.kind).maximum * self.abilities.range();
            float utility = 600 - gap + (gap < range ? 200 : 0) + (100 - p.health) * 2;
            if (p.id == a.target)
                utility += a.targetTime > 0 ? 10000 : 150;
            if (utility > best)
            {
                best = utility;
                target = p.id;
            }
        }
        if (target != a.target)
        {
            a.target = target;
            a.targetTime = .65f;
        }
        auto observation = observeBot(match.world(), i + 1, target);
        if (state.rules.mode == GameMode::Control)
            observation.objective = match.world().arena().controlAnchors()[state.mode.zone];
        auto input = a.controller->update(observation, dt);
        // Avoid friendly fire using actual line and blast risk, without inspecting human input.
        if (state.rules.friendlyFire && input.shoot)
        {
            auto from = self.weaponOrigin();
            for (const auto& p : match.world().players())
                if (p.alive() && match.world().allied(p.id, self.id))
                {
                    auto c = Vec2{p.position.x + p.size.x / 2, p.position.y + p.size.y / 2};
                    float along =
                        (c.x - from.x) * input.aimDirection.x + (c.y - from.y) * input.aimDirection.y;
                    float across = std::abs((c.x - from.x) * input.aimDirection.y -
                                            (c.y - from.y) * input.aimDirection.x);
                    if (along > 0 && along < botWeaponProfile(self.weapon.kind).maximum &&
                        across < p.size.x * .7f)
                        input.shoot = input.triggerHeld = false;
                    if (explosiveWeapon(self.weapon.kind) && observation.target &&
                        std::hypot(c.x - observation.target->position.x,
                                   c.y - observation.target->position.y) < 200)
                        input.shoot = input.triggerHeld = false;
                }
        }
        match.setInput(i + 1, input);
        unsigned mask = (input.activeQ ? 1 : 0) | (input.activeE ? 2 : 0);
        if (input.reload)
            match.action(i + 1, state.round, ++a.sequence, false, false, input.aimDirection, true);
        if (input.jump || (input.shoot && !a.held) || mask)
            match.action(i + 1, state.round, ++a.sequence, input.jump, input.shoot && !a.held,
                         input.aimDirection, false, mask, self.lifeRevision);
        a.held = input.shoot;
        a.stats.seconds += dt;
        if (observation.target)
        {
            float gap = std::hypot(self.position.x - observation.target->position.x,
                                   self.position.y - observation.target->position.y);
            if (gap < botWeaponProfile(self.weapon.kind).maximum * self.abilities.range())
                a.stats.inReach += dt;
        }
    }
}
} // namespace cubeshot
