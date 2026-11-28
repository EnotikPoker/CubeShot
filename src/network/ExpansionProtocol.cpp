#include "network/ExpansionProtocol.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace cubeshot::network
{
namespace
{
bool bounded(float v, float a, float b)
{
    return std::isfinite(v) && v >= a - .001f && v <= b + .001f;
}

void put(wire::Vec* v, Vec2 p)
{
    v->set_x(p.x);
    v->set_y(p.y);
}

bool finite(const wire::Vec& v)
{
    return bounded(v.x(), -100000, 100000) && bounded(v.y(), -100000, 100000);
}

Vec2 get(const wire::Vec& v)
{
    return {v.x(), v.y()};
}
} // namespace

void encodePlayerExpansion(wire::Player& v, const Player& p)
{
    v.set_life(p.lifeRevision);
    for (float x : {p.spawnProtection, p.lastHostileAgo, p.parry, p.slow, p.overdrive, p.cleanse, p.tempering,
                    p.temperCooldown, p.resonance, p.beamTime})
        v.add_effects(x);
    v.set_fresh_mag(p.freshMagazine);
    v.set_economy(p.economyCounter);
    v.set_melee_hits(p.meleeSuccesses);
    v.set_impulse_key(p.impulseKey);
}

bool decodePlayerExpansion(const wire::Player& v, Player& p)
{
    if (v.effects_size() != 10 || v.economy() > 3)
        return false;
    unsigned i = 0;
    float limits[]{1.5f, 999, .35f, 2, 4, 1.5f, 1.5f, 5, 2, 10000};
    for (float* x : {&p.spawnProtection, &p.lastHostileAgo, &p.parry, &p.slow, &p.overdrive, &p.cleanse,
                     &p.tempering, &p.temperCooldown, &p.resonance, &p.beamTime})
    {
        if (!bounded(v.effects(i), 0, limits[i]))
            return false;
        *x = v.effects(i++);
    }
    p.lifeRevision = v.life();
    p.freshMagazine = v.fresh_mag();
    p.economyCounter = v.economy();
    p.meleeSuccesses = v.melee_hits();
    p.impulseKey = v.impulse_key();
    return true;
}

void encodeBulletExpansion(wire::Bullet& v, const Bullet& b)
{
    v.set_owner_life(b.ownerLife);
    v.set_reflections(b.reflections);
    v.set_bounces(b.bounces);
    v.set_secondary(b.secondary);
    v.set_area(b.area);
    v.set_blast_damage(b.blastDamage);
    v.set_blast_radius(b.blastRadius);
    v.set_terrain_damage(b.terrainDamage);
    v.set_terrain_radius(b.terrainRadius);
}

bool decodeBulletExpansion(const wire::Bullet& v, Bullet& b)
{
    if (v.reflections() > 1 || v.bounces() > 3 || v.blast_damage() > 120 || v.terrain_damage() > 200 ||
        !bounded(v.blast_radius(), 0, 240) || !bounded(v.terrain_radius(), 0, 144))
        return false;
    b.ownerLife = v.owner_life();
    b.reflections = v.reflections();
    b.bounces = v.bounces();
    b.secondary = v.secondary();
    b.area = v.area();
    b.blastDamage = v.blast_damage();
    b.blastRadius = v.blast_radius();
    b.terrainDamage = v.terrain_damage();
    b.terrainRadius = v.terrain_radius();
    return true;
}

void encodeExpansion(wire::Snapshot& v, const WorldSnapshot& s)
{
    v.set_format(unsigned(s.rules.format));
    v.set_participants(s.rules.participants);
    v.set_friendly_fire(s.rules.friendlyFire);
    for (unsigned i = 0; i < maxPlayers; ++i)
    {
        v.add_slots(unsigned(s.slots[i]));
        v.add_teams(s.teams[i]);
        v.add_difficulty(s.botDifficulty[i]);
    }
    for (auto x : s.teamScore)
        v.add_team_score(x);
    v.set_winner_team(s.winnerTeam);
    v.set_host(s.host);
    v.set_decision_remaining(s.decisionRemaining);
    v.set_rules_mode(unsigned(s.rules.mode));
    v.set_stages(s.rules.stages);
    v.set_control_goal(s.rules.controlGoal);
    auto m = v.mutable_mode_state();
    m->set_chaos(unsigned(s.mode.chaos));
    m->set_next_chaos(unsigned(s.mode.nextChaos));
    m->set_elapsed(s.mode.elapsed);
    m->set_zone_remaining(s.mode.zoneRemaining);
    m->set_zone(s.mode.zone);
    for (auto c : s.mode.control)
        m->add_control(c);
    for (auto r : s.mode.respawn)
    {
        auto p = m->add_respawn();
        p->set_life(r.life);
        p->set_remaining(r.remaining);
        p->set_decision(r.decisionTime);
        p->set_choosing(r.choosing);
    }
    for (const auto& e : s.entities)
    {
        auto x = v.add_entities();
        x->set_id(e.id);
        x->set_kind(unsigned(e.kind));
        put(x->mutable_position(), e.position);
        put(x->mutable_velocity(), e.velocity);
        put(x->mutable_size(), e.size);
        put(x->mutable_anchor(), e.anchor);
        x->set_life(e.life);
        x->set_fuse(e.fuse);
        x->set_arm(e.arm);
        x->set_health(e.health);
        x->set_attached(e.attached);
        x->set_attached_life(e.attachedLife);
        x->set_anchored(e.anchored);
        auto b = x->mutable_source();
        encodeBulletExpansion(*b, e.attack);
        b->set_shooter(e.attack.shooterId);
        b->set_volley(e.attack.volley);
        b->set_weapon(unsigned(e.attack.weapon));
        b->set_active(unsigned(e.attack.active));
        b->set_power(e.attack.power);
        b->set_terrain_power(e.attack.terrainPower);
        b->set_blast_scale(e.attack.blastScale);
        b->set_ignites(e.attack.ignites);
        b->set_damage(e.attack.damage);
    }
}

bool decodeExpansion(const wire::Snapshot& v, WorldSnapshot& s)
{
    s.rules.format = MatchFormat(v.format());
    s.rules.participants = v.participants();
    s.rules.friendlyFire = v.friendly_fire();
    if (v.slots_size() != maxPlayers || v.teams_size() != maxPlayers || v.difficulty_size() != maxPlayers ||
        v.team_score_size() != 2 || v.winner_team() > 2 || !v.host() || v.host() > maxPlayers ||
        !bounded(v.decision_remaining(), 0, 30))
        return false;
    s.host = v.host();
    s.winnerTeam = v.winner_team();
    s.decisionRemaining = v.decision_remaining();
    for (unsigned i = 0; i < maxPlayers; ++i)
    {
        if (v.slots(i) > 2 || v.teams(i) < 1 || v.teams(i) > 2 || v.difficulty(i) > 2)
            return false;
        s.slots[i] = SlotKind(v.slots(i));
        s.teams[i] = v.teams(i);
        s.botDifficulty[i] = v.difficulty(i);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        if (v.team_score(i) > 240)
            return false;
        s.teamScore[i] = v.team_score(i);
    }
    s.rules.mode = GameMode(v.rules_mode());
    s.rules.stages = v.stages();
    s.rules.controlGoal = v.control_goal();
    if (!s.rules.valid() || s.host > s.rules.participants ||
        (s.rules.format != MatchFormat::Teams && s.winnerTeam) ||
        (s.rules.format == MatchFormat::Teams && v.winner()) || v.winner() > s.rules.participants)
        return false;
    for (unsigned i = 0; i < maxPlayers; ++i)
    {
        if (i >= s.rules.participants && s.slots[i] != SlotKind::Empty)
            return false;
        if (v.connected_size() == maxPlayers && v.connected(i) != (s.slots[i] != SlotKind::Empty))
            return false;
    }
    auto m = v.mode_state();
    if (m.chaos() > 6 || m.next_chaos() > 6 || m.zone() > 2 || m.control_size() != maxPlayers ||
        m.respawn_size() != maxPlayers || !bounded(m.elapsed(), 0, 480.1f) ||
        !bounded(m.zone_remaining(), 0, 30))
        return false;
    s.mode.chaos = ChaosKind(m.chaos());
    s.mode.nextChaos = ChaosKind(m.next_chaos());
    s.mode.elapsed = m.elapsed();
    s.mode.zoneRemaining = m.zone_remaining();
    s.mode.zone = m.zone();
    for (unsigned i = 0; i < maxPlayers; ++i)
    {
        if (!bounded(m.control(i), 0, 241) || !bounded(m.respawn(i).remaining(), 0, 3) ||
            !bounded(m.respawn(i).decision(), 0, 10))
            return false;
        s.mode.control[i] = m.control(i);
        s.mode.respawn[i] = {m.respawn(i).life(), m.respawn(i).remaining(), m.respawn(i).decision(),
                             m.respawn(i).choosing()};
    }
    if (v.entities_size() > maxWorldEntities)
        return false;
    std::set<std::uint64_t> ids;
    for (auto x : v.entities())
    {
        if (!x.id() || !ids.insert(x.id()).second || x.kind() > 3 || !finite(x.position()) ||
            !finite(x.velocity()) || !finite(x.size()) || !finite(x.anchor()) ||
            !bounded(x.size().x(), 1, 32) || !bounded(x.size().y(), 1, 112) || !bounded(x.life(), -1, 12) ||
            !bounded(x.fuse(), 0, 1.251f) || !bounded(x.arm(), 0, .601f) || !x.health() || x.health() > 90 ||
            x.attached() > maxPlayers)
            return false;
        auto b = x.source();
        if (b.shooter() > maxPlayers || b.weapon() >= weaponCount || !validActive(ActiveKind(b.active())) ||
            !bounded(b.power(), 1, 2.72f) || !bounded(b.terrain_power(), 1, 3.75f) ||
            !bounded(b.blast_scale(), 1, 1.4f) || b.damage() > 100)
            return false;
        WorldEntity e;
        e.id = x.id();
        e.kind = EntityKind(x.kind());
        e.position = get(x.position());
        e.velocity = get(x.velocity());
        e.size = get(x.size());
        e.anchor = get(x.anchor());
        e.life = x.life();
        e.fuse = x.fuse();
        e.arm = x.arm();
        e.health = x.health();
        e.attached = x.attached();
        e.attachedLife = x.attached_life();
        e.anchored = x.anchored();
        if (!decodeBulletExpansion(b, e.attack))
            return false;
        e.attack.shooterId = b.shooter();
        e.attack.volley = b.volley();
        e.attack.weapon = WeaponKind(b.weapon());
        e.attack.active = ActiveKind(b.active());
        e.attack.power = b.power();
        e.attack.terrainPower = b.terrain_power();
        e.attack.blastScale = b.blast_scale();
        e.attack.ignites = b.ignites();
        e.attack.damage = b.damage();
        s.entities.push_back(e);
    }
    return true;
}
} // namespace cubeshot::network
