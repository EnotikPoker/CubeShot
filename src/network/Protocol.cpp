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
    }
    return out;
}

std::optional<WorldSnapshot> decodeSnapshot(const wire::Snapshot& in)
{
    if (in.phase() > static_cast<unsigned>(MatchPhase::Finished) || in.aborted() > 2 || in.winner() > 2 ||
        in.score_size() != 2 || in.connected_size() != 2 || in.ready_size() != 2 || in.players_size() != 2 ||
        in.names_size() != 2 || in.input_ack_size() != 2 || in.action_ack_size() != 2 ||
        in.bullets_size() > 64 || !std::isfinite(in.remaining()) || in.remaining() < 0 ||
        in.remaining() > 3.01f)
        return {};
    if (!validMap(static_cast<MapId>(in.map())) || in.votes_size() != 2)
        return {};
    for (auto vote : in.votes())
        if (vote < -1 || vote > static_cast<int>(MapId::NatureLarge))
            return {};
    WorldSnapshot s;
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
            !std::isfinite(v.cooldown()) || v.cooldown() < 0 || v.cooldown() > 1)
            return {};
        Player p;
        p.id = v.id();
        p.position = vec(v.position());
        p.velocity = vec(v.velocity());
        p.aimDirection = vec(v.aim());
        p.health = v.health();
        p.grounded = v.grounded();
        p.weapon.cooldown = v.cooldown();
        s.players.push_back(p);
    }
    std::set<BulletId> bullets;
    for (const auto& v : in.bullets())
    {
        if (!v.id() || !bullets.insert(v.id()).second || !finite(v.position()) || !finite(v.velocity()) ||
            v.shooter() < 1 || v.shooter() > 2 || !std::isfinite(v.lifetime()) || v.lifetime() <= 0 ||
            v.lifetime() > 3)
            return {};
        Bullet b;
        b.id = v.id();
        b.position = vec(v.position());
        b.velocity = vec(v.velocity());
        b.shooterId = v.shooter();
        b.lifetime = v.lifetime();
        b.shotSequence = v.shot_sequence();
        s.bullets.push_back(b);
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
        if (!e.id() || !e.round() || e.kind() > 3 || e.actor() < 1 || e.actor() > 2 || e.target() > 2 ||
            !finite(e.position()) || !validAim(e.direction()))
            return {};
        result.push_back({e.id(), e.tick(), e.shot_sequence(), e.round(), static_cast<EventKind>(e.kind()),
                          e.actor(), e.target(), vec(e.position()), vec(e.direction())});
    }
    return result;
}
} // namespace cubeshot::network
