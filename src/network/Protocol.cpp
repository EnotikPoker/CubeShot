#include "network/Protocol.h"
#include "network/Identity.h"
#include <cmath>
#include <set>

namespace cubeshot::network
{
namespace
{
void vec(wire::Vec* out, Vec2 value)
{
    out->set_x(value.x);
    out->set_y(value.y);
}

Vec2 vec(const wire::Vec& value)
{
    return {value.x(), value.y()};
}

bool finite(const wire::Vec& v)
{
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::abs(v.x()) < 100000 &&
           std::abs(v.y()) < 100000;
}

void encodePiece(wire::Piece* out, const PhysicsPiece& p)
{
    out->set_id(p.id);
    out->set_owner(p.owner);
    vec(out->mutable_position(), p.position);
    vec(out->mutable_velocity(), p.velocity);
    vec(out->mutable_size(), p.size);
    out->set_angle(p.angle);
    out->set_spin(p.spin);
}

std::optional<PhysicsPiece> decodePiece(const wire::Piece& p)
{
    if (!p.id() || p.owner() < 1 || p.owner() > 2 || !finite(p.position()) || !finite(p.velocity()) ||
        !finite(p.size()) || p.size().x() <= 0 || p.size().y() <= 0 || p.size().x() > 64 ||
        p.size().y() > 64 || !std::isfinite(p.angle()) || std::abs(p.angle()) > 3.15f ||
        !std::isfinite(p.spin()) || std::abs(p.spin()) > 40)
        return {};
    return PhysicsPiece{p.id(),        p.owner(), vec(p.position()), vec(p.velocity()),
                        vec(p.size()), p.angle(), p.spin()};
}
} // namespace

wire::Envelope envelope()
{
    wire::Envelope e;
    e.set_protocol(protocolVersion);
    return e;
}

bool decode(const void* data, int size, wire::Envelope& result)
{
    return size > 0 && size <= maxPacketBytes && result.ParseFromArray(data, size) &&
           result.protocol() == protocolVersion && result.body_case() != wire::Envelope::BODY_NOT_SET;
}

bool validAim(const wire::Vec& aim)
{
    return finite(aim) && std::hypot(aim.x(), aim.y()) > 0.0001f;
}

wire::Snapshot encodeSnapshot(const WorldSnapshot& s)
{
    wire::Snapshot out;
    out.set_map(static_cast<unsigned>(s.map));
    out.set_voting(s.voting);
    for (auto vote : s.votes)
        out.add_votes(vote);
    out.set_selection_revision(s.selectionRevision);
    out.set_state_sequence(s.stateSequence);
    out.set_tick(s.tick);
    out.set_round(s.round);
    out.set_phase(static_cast<unsigned>(s.phase));
    out.set_remaining(s.remaining);
    out.set_winner(s.winner);
    out.set_aborted(static_cast<unsigned>(s.aborted));
    for (const auto& name : s.names)
        out.add_names(name);
    for (auto n : s.inputAck)
        out.add_input_ack(n);
    for (auto n : s.actionAck)
        out.add_action_ack(n);
    for (auto n : s.score)
        out.add_score(n);
    for (auto n : s.connected)
        out.add_connected(n);
    for (auto n : s.ready)
        out.add_ready(n);
    for (unsigned i = 0; i < 2; ++i)
    {
        out.add_loadout(static_cast<unsigned>(s.loadout[i]));
        out.add_rolled(static_cast<unsigned>(s.rolled[i]));
        out.add_weapon_choice(static_cast<unsigned>(s.weaponChoice[i]));
        out.add_spin_remaining(s.spinRemaining[i]);
    }
    for (const auto& fire : s.fires)
    {
        auto* v = out.add_fires();
        v->set_id(fire.id);
        v->set_owner(fire.owner);
        vec(v->mutable_position(), fire.position);
        v->set_life(fire.life);
        v->set_radius(fire.radius);
    }
    for (const auto& corpse : s.corpses)
    {
        auto* v = out.add_corpses();
        encodePiece(v->mutable_body(), corpse.body);
        v->set_integrity(corpse.integrity);
        v->set_cells(corpse.cells);
    }
    for (const auto& piece : s.pieces)
        encodePiece(out.add_pieces(), piece);
    for (const auto& p : s.players)
    {
        auto* v = out.add_players();
        v->set_id(p.id);
        vec(v->mutable_position(), p.position);
        vec(v->mutable_velocity(), p.velocity);
        vec(v->mutable_aim(), p.aimDirection);
        v->set_health(p.health);
        v->set_grounded(p.grounded);
        v->set_cooldown(p.weapon.cooldown);
        v->set_weapon(static_cast<unsigned>(p.weapon.kind));
        v->set_preparation(p.weapon.preparation);
        v->set_charging(p.weapon.charging);
        v->set_shots(p.weapon.shots);
        v->set_spin(p.weapon.spin);
        v->set_heat(p.weapon.heat);
        v->set_overheated(p.weapon.overheated);
        v->set_burning(p.burning);
        v->set_burn_owner(p.burnOwner);
        v->set_damaged_cells(p.damagedCells);
    }
    for (const auto& b : s.bullets)
    {
        auto* v = out.add_bullets();
        v->set_id(b.id);
        vec(v->mutable_position(), b.position);
        vec(v->mutable_velocity(), b.velocity);
        v->set_shooter(b.shooterId);
        v->set_lifetime(b.lifetime);
        v->set_shot_sequence(b.shotSequence);
        v->set_weapon(static_cast<unsigned>(b.weapon));
        v->set_damage(b.damage);
        v->set_gravity(b.acceleration);
        v->set_volley(b.volley);
        v->set_age(b.age);
        v->set_returning(b.returning);
        v->set_hit_mask(b.hitMask);
    }
    return out;
}

std::optional<WorldSnapshot> decodeSnapshot(const wire::Snapshot& in)
{
    if (in.phase() > static_cast<unsigned>(MatchPhase::Armory) || in.aborted() > 2 || in.winner() > 2 ||
        in.score_size() != 2 || in.connected_size() != 2 || in.ready_size() != 2 || in.players_size() != 2 ||
        in.names_size() != 2 || in.input_ack_size() != 2 || in.action_ack_size() != 2 ||
        in.bullets_size() > 256 || !std::isfinite(in.remaining()) || in.remaining() < 0 ||
        in.remaining() > 4.01f)
        return {};
    if (!validMap(static_cast<MapId>(in.map())) || in.votes_size() != 2)
        return {};
    for (auto vote : in.votes())
        if (vote < -1 || vote > static_cast<int>(MapId::NatureLarge))
            return {};
    if (in.loadout_size() != 2 || in.rolled_size() != 2 || in.weapon_choice_size() != 2 ||
        in.spin_remaining_size() != 2 || in.corpses_size() > 2 || in.pieces_size() > Debris::maxPieces)
        return {};
    if (in.fires_size() > static_cast<int>(maxFires))
        return {};
    WorldSnapshot s;
    std::set<std::uint64_t> fires;
    for (const auto& f : in.fires())
    {
        if (!f.id() || !fires.insert(f.id()).second || f.owner() < 1 || f.owner() > 2 ||
            !finite(f.position()) || !std::isfinite(f.life()) || f.life() <= 0 || f.life() > 3.01f ||
            !std::isfinite(f.radius()) || f.radius() < 12 || f.radius() > 34.01f)
            return {};
        s.fires.push_back({f.id(), f.owner(), vec(f.position()), f.life(), f.radius()});
    }
    s.map = static_cast<MapId>(in.map());
    s.voting = in.voting();
    s.votes = {{in.votes(0), in.votes(1)}};
    s.selectionRevision = in.selection_revision();
    s.stateSequence = in.state_sequence();
    s.tick = in.tick();
    s.round = in.round();
    s.phase = static_cast<MatchPhase>(in.phase());
    s.remaining = in.remaining();
    s.winner = in.winner();
    s.aborted = static_cast<AbortReason>(in.aborted());
    for (int i = 0; i < 2; ++i)
    {
        if (!validNickname(in.names(i)))
            return {};
        s.names[i] = in.names(i);
        if (in.score(i) > Match::winsRequired)
            return {};
        if (in.loadout(i) >= weaponCount || in.rolled(i) >= weaponCount || in.weapon_choice(i) > 2 ||
            !std::isfinite(in.spin_remaining(i)) || in.spin_remaining(i) < 0 ||
            in.spin_remaining(i) > Match::spinSeconds + .01f)
            return {};
        s.loadout[i] = static_cast<WeaponKind>(in.loadout(i));
        s.rolled[i] = static_cast<WeaponKind>(in.rolled(i));
        s.weaponChoice[i] = static_cast<WeaponChoice>(in.weapon_choice(i));
        s.spinRemaining[i] = in.spin_remaining(i);
        s.inputAck[i] = in.input_ack(i);
        s.actionAck[i] = in.action_ack(i);
        s.score[i] = in.score(i);
        s.connected[i] = in.connected(i);
        s.ready[i] = in.ready(i);
    }
    std::set<PlayerId> players;
    for (const auto& v : in.players())
    {
        if (v.id() < 1 || v.id() > 2 || !players.insert(v.id()).second || v.health() > Player::maxHealth ||
            !finite(v.position()) || !finite(v.velocity()) || !validAim(v.aim()) ||
            !std::isfinite(v.cooldown()) || v.cooldown() < 0 || v.cooldown() > 2.21f ||
            v.weapon() >= weaponCount || !std::isfinite(v.preparation()) || v.preparation() < 0 ||
            v.preparation() > .301f)
            return {};
        if (!std::isfinite(v.spin()) || v.spin() < 0 || v.spin() > 1 || !std::isfinite(v.heat()) ||
            v.heat() < 0 || v.heat() > 1 || !std::isfinite(v.burning()) || v.burning() < 0 ||
            v.burning() > 2.51f || v.burn_owner() > 2 || v.damaged_cells() > 0xffff)
            return {};
        Player p;
        p.id = v.id();
        p.position = vec(v.position());
        p.velocity = vec(v.velocity());
        p.aimDirection = vec(v.aim());
        p.health = v.health();
        p.grounded = v.grounded();
        p.weapon.equip(static_cast<WeaponKind>(v.weapon()));
        p.weapon.cooldown = v.cooldown();
        p.weapon.preparation = v.preparation();
        p.weapon.charging = v.charging();
        p.weapon.shots = v.shots();
        p.weapon.spin = v.spin();
        p.weapon.heat = v.heat();
        p.weapon.overheated = v.overheated();
        p.burning = v.burning();
        p.burnOwner = v.burn_owner();
        p.damagedCells = v.damaged_cells();
        s.players.push_back(p);
    }
    std::set<BulletId> bullets;
    for (const auto& v : in.bullets())
    {
        if (!v.id() || !bullets.insert(v.id()).second || !finite(v.position()) || !finite(v.velocity()) ||
            v.shooter() < 1 || v.shooter() > 2 || !std::isfinite(v.lifetime()) || v.lifetime() <= 0 ||
            v.lifetime() > 4 || v.weapon() >= weaponCount || !v.damage() || v.damage() > 100 ||
            !std::isfinite(v.gravity()) || v.gravity() < -35 || v.gravity() > 500)
            return {};
        if (!std::isfinite(v.age()) || v.age() < 0 || v.age() > 4.01f || v.hit_mask() > 0x6)
            return {};
        Bullet b;
        b.id = v.id();
        b.position = vec(v.position());
        b.velocity = vec(v.velocity());
        b.shooterId = v.shooter();
        b.lifetime = v.lifetime();
        b.shotSequence = v.shot_sequence();
        b.weapon = static_cast<WeaponKind>(v.weapon());
        b.damage = v.damage();
        b.acceleration = v.gravity();
        b.volley = v.volley();
        b.age = v.age();
        b.returning = v.returning();
        b.hitMask = v.hit_mask();
        s.bullets.push_back(b);
    }
    std::set<std::uint64_t> pieces;
    std::set<PlayerId> corpseOwners;
    for (const auto& v : in.corpses())
    {
        auto body = decodePiece(v.body());
        if (!body || !pieces.insert(body->id).second || !corpseOwners.insert(body->owner).second ||
            v.integrity() > 100 || v.cells() > 0xffff || (v.integrity() == 0 && v.cells() != 0))
            return {};
        s.corpses.push_back({*body, static_cast<int>(v.integrity()), v.cells()});
    }
    for (const auto& v : in.pieces())
    {
        auto piece = decodePiece(v);
        if (!piece || !pieces.insert(piece->id).second)
            return {};
        s.pieces.push_back(*piece);
    }
    return s;
}

wire::Events encodeEvents(const std::vector<GameEvent>& events)
{
    wire::Events out;
    for (const auto& e : events)
    {
        auto* v = out.add_events();
        v->set_id(e.id);
        v->set_tick(e.tick);
        v->set_shot_sequence(e.shotSequence);
        v->set_round(e.round);
        v->set_kind(static_cast<unsigned>(e.kind));
        v->set_actor(e.actor);
        v->set_target(e.target);
        v->set_damage(e.damage);
        v->set_weapon(static_cast<unsigned>(e.weapon));
        v->set_volley(e.volley);
        vec(v->mutable_position(), e.position);
        vec(v->mutable_direction(), e.direction);
    }
    return out;
}

std::optional<std::vector<GameEvent>> decodeEvents(const wire::Events& in)
{
    if (in.events_size() > 256)
        return {};
    std::vector<GameEvent> result;
    for (const auto& e : in.events())
    {
        if (!e.id() || !e.round() || e.kind() > 6 || e.actor() < 1 || e.actor() > 2 || e.target() > 2 ||
            !finite(e.position()) || !validAim(e.direction()) || e.damage() > 1000 ||
            e.weapon() >= weaponCount)
            return {};
        result.push_back({e.id(), e.tick(), e.shot_sequence(), e.round(), static_cast<EventKind>(e.kind()),
                          e.actor(), e.target(), vec(e.position()), vec(e.direction()),
                          static_cast<int>(e.damage()), static_cast<WeaponKind>(e.weapon()), e.volley()});
    }
    return result;
}
} // namespace cubeshot::network
