#include "client/SoundScene.h"
#include <algorithm>
#include <cmath>

namespace cubeshot::client
{
void SoundScene::clear()
{
    bodies_.clear();
    warnings_.clear();
    boomerangs_.clear();
    seen_.clear();
    shots_.clear();
    casts_.clear();
    time_ = 0;
    confirmAt_ = impactAt_ = blastAt_ = rollAt_ = -1;
    round_ = 0;
    countdown_ = -1;
    phase_ = MatchPhase::Lobby;
    choice_ = WeaponChoice::Ready;
}

void SoundScene::at(SoundCue cue, Vec2 point, const Arena& arena, float gain, unsigned priority)
{
    if (budget_ >= 12 && priority < 3)
        return;
    const float width = std::max(1.f, arena.bounds.size.x);
    const float pan = std::clamp((point.x - arena.bounds.position.x) / width * 1.8f - .9f, -.9f, .9f);
    if (mixer_.play(cue, gain, pan, priority, 1.f + .018f * std::sin(time_ * 137 + budget_)))
        ++budget_;
}

bool SoundScene::fresh(const GameEvent& e, PlayerId own)
{
    if (e.id && !seen_.insert({e.round, e.id}).second)
        return false;
    if (seen_.size() > 1024)
        seen_.erase(seen_.begin());
    if (e.kind == EventKind::Shot && e.actor == own && (e.volley || e.shotSequence))
    {
        ShotKey key{e.round, e.actor, e.ownerLife, e.volley ? e.volley : e.shotSequence};
        if (!shots_.emplace(key, time_).second)
            return false;
    }
    if (e.kind == EventKind::ActiveCast && e.actor == own && e.shotSequence)
    {
        CastKey key{e.round, e.actor, e.ownerLife, e.shotSequence, e.active, e.radius > 0};
        if (!casts_.emplace(key, time_).second)
            return false;
    }
    return true;
}

void SoundScene::match(MatchPhase phase, unsigned round, float remaining,
                       const std::array<WeaponChoice, 2>& choices, PlayerId own)
{
    if (round != round_)
    {
        round_ = round;
        countdown_ = -1;
        bodies_.clear();
    }
    if (phase == MatchPhase::Countdown)
    {
        int seconds = int(std::ceil(remaining));
        if (seconds != countdown_ && seconds > 0 && seconds <= 3)
            mixer_.play(SoundCue::Countdown, .65f, 0, 3);
        countdown_ = seconds;
    }
    if (phase != phase_)
    {
        if (phase == MatchPhase::Round)
            mixer_.play(SoundCue::RoundStart, .7f, 0, 3);
        if (phase == MatchPhase::RoundEnd || phase == MatchPhase::Finished)
            mixer_.play(SoundCue::RoundEnd, .65f, 0, 3);
        phase_ = phase;
    }
    if (own >= 1 && own <= 2)
    {
        auto choice = choices[own - 1];
        if (choice == WeaponChoice::Spinning && time_ - rollAt_ > .13f)
        {
            mixer_.play(SoundCue::Roll, .35f, 0, 1);
            rollAt_ = time_;
        }
        if (choice == WeaponChoice::Revealing && choice_ == WeaponChoice::Spinning)
            mixer_.play(SoundCue::Reward, .6f, 0, 3);
        choice_ = choice;
    }
}

void SoundScene::update(float dt, const std::vector<Player>& players, const Arena& arena,
                        const std::vector<GameEvent>& events, PlayerId own,
                        const std::vector<WorldEntity>& entities, const std::vector<Bullet>& bullets)
{
    dt = std::clamp(dt, 0.f, .1f);
    time_ += dt;
    budget_ = 0;
    for (const auto& e : events)
    {
        auto target = std::find_if(players.begin(), players.end(), [&](auto& p) { return p.id == e.target; });
        auto source = std::find_if(players.begin(), players.end(), [&](auto& p) { return p.id == e.actor; });
        if (e.target && e.kind != EventKind::CorpseHit && e.kind != EventKind::CorpseBreak &&
            target != players.end() && target->lifeRevision != e.targetLife)
            continue;
        if ((e.kind == EventKind::Shot || e.kind == EventKind::ActiveCast) && source != players.end() &&
            source->lifeRevision != e.ownerLife)
            continue;
        if (!fresh(e, own))
            continue;
        const float gain = e.actor == own ? .9f : .6f;
        switch (e.kind)
        {
            case EventKind::Shot:
                at(weaponSound(e.weapon), e.position, arena, gain, e.actor == own ? 2 : 1);
                break;
            case EventKind::Hit:
                if (e.actor == own && e.target != own && time_ - confirmAt_ >= .055f)
                {
                    mixer_.play(SoundCue::HitConfirm, .65f, 0, 3);
                    confirmAt_ = time_;
                }
                if (time_ - impactAt_ > .035f)
                {
                    at(SoundCue::Flesh, e.position, arena, e.target == own ? .7f : .35f);
                    impactAt_ = time_;
                }
                break;
            case EventKind::ShieldBlock:
                at(SoundCue::Shield, e.position, arena, .55f, 2);
                break;
            case EventKind::Death:
                at(SoundCue::Death, e.position, arena, .6f, 2);
                if (e.actor == own && e.target != own)
                    mixer_.play(SoundCue::KillConfirm, .75f, 0, 3);
                break;
            case EventKind::SurfaceImpact:
                if (time_ - impactAt_ > .045f)
                {
                    auto cell = arena.terrainAt(e.position, 12);
                    auto cue = !cell                               ? SoundCue::Stone
                               : cell->material == Material::Metal ? SoundCue::Metal
                               : cell->material == Material::Wood  ? SoundCue::Wood
                               : cell->material == Material::Glass ? SoundCue::Glass
                                                                   : SoundCue::Stone;
                    at(cue, e.position, arena, .32f);
                    impactAt_ = time_;
                }
                break;
            case EventKind::Explosion:
                if (time_ - blastAt_ > .045f)
                {
                    at(SoundCue::Explosion, e.position, arena, .9f, 2);
                    blastAt_ = time_;
                }
                break;
            case EventKind::CorpseBreak:
                at(SoundCue::Break, e.position, arena, .45f);
                break;
            case EventKind::CorpseHit:
                break;
            case EventKind::ElectricArc:
                break; // The weapon/cast already has its own sound.
            case EventKind::ActiveCast:
            {
                if (e.active == ActiveKind::Slam && e.radius > 0)
                {
                    at(SoundCue::Land, e.position, arena, .8f, 2);
                    break;
                }
                auto cue = e.active == ActiveKind::Heal                                    ? SoundCue::Heal
                           : e.active == ActiveKind::Blink || e.active == ActiveKind::Dash ? SoundCue::Blink
                           : e.active == ActiveKind::Parry                                 ? SoundCue::Parry
                           : e.active == ActiveKind::IceGrenade                            ? SoundCue::Ice
                           : e.active == ActiveKind::Mine                                  ? SoundCue::Mine
                           : e.active == ActiveKind::Screen || e.active == ActiveKind::Barrier
                               ? SoundCue::Screen
                           : e.active == ActiveKind::Overdrive ? SoundCue::Boost
                           : e.active == ActiveKind::Cleanse   ? SoundCue::Cleanse
                                                               : SoundCue::Cast;
                at(cue, e.position, arena, gain, 2);
                break;
            }
        }
    }
    for (const auto& p : players)
    {
        auto found = bodies_.find(p.id);
        if (found != bodies_.end() && found->second.life == p.lifeRevision && dt > 0)
        {
            auto& old = found->second;
            const float distance = std::hypot(p.position.x - old.position.x, p.position.y - old.position.y);
            const float gain = p.id == own ? .45f : .28f;
            if (p.alive() && old.alive && distance < 100)
            {
                if (p.grounded && old.grounded && std::abs(p.velocity.x) > 40)
                {
                    old.step += std::abs(p.position.x - old.position.x);
                    if (old.step > 52)
                    {
                        at(SoundCue::Step, p.position, arena, gain);
                        old.step = 0;
                    }
                }
                if (!p.grounded && old.grounded && p.velocity.y < -80)
                    at(SoundCue::Jump, p.position, arena, gain);
                if (p.grounded && !old.grounded)
                    at(SoundCue::Land, p.position, arena, gain);
                if (p.weapon.kind == old.weapon)
                {
                    if (p.weapon.reloading && !old.reload)
                        at(SoundCue::Reload, p.position, arena, gain);
                    if (p.weapon.reloading && p.weapon.ammo > old.ammo)
                        at(SoundCue::Shell, p.position, arena, gain);
                    if (!p.weapon.reloading && old.reload && p.weapon.ammo > old.ammo)
                        at(SoundCue::Ready, p.position, arena, gain);
                    if (p.weapon.charging && !old.charge)
                        at(SoundCue::Charge, p.position, arena, gain);
                    if (p.weapon.overheated && !old.hot)
                        at(SoundCue::Overheat, p.position, arena, .65f, 2);
                }
                old.motor -= dt;
                old.jet -= dt;
                if (p.weapon.kind == WeaponKind::Minigun && p.weapon.spin > .05f && !p.weapon.overheated &&
                    old.motor <= 0)
                {
                    at(SoundCue::Spin, p.position, arena, .18f * p.weapon.spin);
                    old.motor = .13f;
                }
                if (p.jetpackActive && old.jet <= 0)
                {
                    at(SoundCue::Jetpack, p.position, arena, .25f);
                    old.jet = .13f;
                }
            }
        }
        auto& old = bodies_[p.id];
        if (old.life != p.lifeRevision || !p.alive())
            old.step = old.motor = old.jet = 0;
        old.position = p.position;
        old.life = p.lifeRevision;
        old.grounded = p.grounded;
        old.alive = p.alive();
        old.reload = p.weapon.reloading;
        old.hot = p.weapon.overheated;
        old.charge = p.weapon.charging;
        old.ammo = p.weapon.ammo;
        old.weapon = p.weapon.kind;
    }
    std::map<std::pair<bool, std::uint64_t>, float> next;
    for (const auto& e : entities)
        if (e.kind == EntityKind::Sticky || (e.kind == EntityKind::Mine && e.arm <= 0))
        {
            const auto key = std::make_pair(false, e.id);
            float timer = warnings_.count(key) ? warnings_[key] - dt : 0;
            if (timer <= 0)
            {
                at(SoundCue::Warning, e.position, arena, e.kind == EntityKind::Sticky ? .35f : .15f);
                timer = e.kind == EntityKind::Sticky ? std::clamp(e.fuse * .45f, .10f, .4f) : 1.f;
            }
            next[key] = timer;
        }
    for (const auto& b : bullets)
        if (b.weapon == WeaponKind::GrenadeLauncher && b.lifetime <= .45f)
        {
            const auto key = std::make_pair(true, b.id);
            float timer = warnings_.count(key) ? warnings_[key] - dt : 0;
            if (timer <= 0)
            {
                at(SoundCue::Warning, b.position, arena, .3f);
                timer = .12f;
            }
            next[key] = timer;
        }
    warnings_ = std::move(next);
    std::map<BulletId, bool> flying;
    for (const auto& b : bullets)
        if (b.weapon == WeaponKind::FireBoomerang)
        {
            auto previous = boomerangs_.find(b.id);
            if (previous != boomerangs_.end() && previous->second != b.returning && b.returning)
                at(SoundCue::Return, b.position, arena, .35f);
            flying[b.id] = b.returning;
        }
    boomerangs_ = std::move(flying);
    for (auto it = shots_.begin(); it != shots_.end();)
        if (time_ - it->second > 10)
            it = shots_.erase(it);
        else
            ++it;
    for (auto it = casts_.begin(); it != casts_.end();)
        if (time_ - it->second > 10)
            it = casts_.erase(it);
        else
            ++it;
}
} // namespace cubeshot::client
