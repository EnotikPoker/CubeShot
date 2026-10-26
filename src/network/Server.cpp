#include "network/Server.h"
#include "network/Identity.h"
#include "network/detail/GnsRuntime.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <stdexcept>
#include <utility>

namespace cubeshot::network
{
using detail::age;
using detail::Clock;
using detail::GnsRuntime;
using detail::options;
using detail::send;

struct Server::Impl
{
    GnsRuntime runtime;
    HSteamListenSocket listener = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup group = k_HSteamNetPollGroup_Invalid;

    struct Peer
    {
        PlayerId id = 0;
        Clock::time_point created = Clock::now(), packet = created, input = created;
        std::uint64_t controlSequence = 0, actionSequence = 0, receivedAction = 0;
        std::map<std::uint64_t, PlayerInput> commands;
        std::deque<wire::Action> actions;
        unsigned gapTicks = 0;
        bool hello = false;
    };

    std::map<HSteamNetConnection, Peer> peers;
    Match match;
    std::string token;
    AdmitPlayer admission;
    std::array<std::string, 2> names{"Игрок 1", "Игрок 2"};
    std::uint16_t port = 0;
    bool hostGone = false;
    std::uint64_t stateSequence = 0;

    explicit Impl(std::uint16_t requested, std::string owner, AdmitPlayer admit)
        : match(static_cast<bool>(admit)), token(std::move(owner)), admission(std::move(admit))
    {
        // LAN v1 uses IPv4. An unspecified IPv6 address lets GNS retry IPv4
        // after a failed bind; on Windows that can hide an occupied dual-stack port.
        SteamNetworkingIPAddr address;
        address.SetIPv4(0, requested);
        SteamNetworkingConfigValue_t config[4];
        options(config);
        group = SteamNetworkingSockets()->CreatePollGroup();
        listener = SteamNetworkingSockets()->CreateListenSocketIP(address, 4, config);
        if (!group || !listener)
        {
            if (group)
                SteamNetworkingSockets()->DestroyPollGroup(group);
            if (listener)
                SteamNetworkingSockets()->CloseListenSocket(listener);
            throw std::runtime_error("Cannot bind game port (already in use?)");
        }
        SteamNetworkingSockets()->GetListenSocketAddress(listener, &address);
        port = address.m_port;
        detail::setListenerHandler(listener, [this](auto* event) { onChanged(event); });
    }

    ~Impl()
    {
        detail::removeListenerHandler(listener);
        for (const auto& entry : peers)
        {
            detail::removeConnectionHandler(entry.first);
            SteamNetworkingSockets()->CloseConnection(entry.first, 1000, "Room closed by host", false);
        }
        SteamNetworkingSockets()->CloseListenSocket(listener);
        SteamNetworkingSockets()->DestroyPollGroup(group);
    }

    void remove(HSteamNetConnection c, const char* reason, AbortReason abort = AbortReason::PeerLeft)
    {
        auto found = peers.find(c);
        if (found == peers.end())
            return;
        const auto id = found->second.id;
        if (id)
        {
            match.disconnect(id, abort);
            names[id - 1] = "Игрок " + std::to_string(id);
            if (!token.empty() && id == 1)
                hostGone = true;
        }
        detail::removeConnectionHandler(c);
        SteamNetworkingSockets()->CloseConnection(c, 1000, reason, false);
        peers.erase(found);
        broadcast(true);
    }

    void onChanged(SteamNetConnectionStatusChangedCallback_t* event)
    {
        const auto c = event->m_hConn;
        if (event->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting)
        {
            if (peers.size() >= 4 || hostGone || SteamNetworkingSockets()->AcceptConnection(c) != k_EResultOK)
            {
                SteamNetworkingSockets()->CloseConnection(c, 1000, "Room full", false);
                return;
            }
            SteamNetworkingSockets()->SetConnectionPollGroup(c, group);
            peers.emplace(c, Peer{});
            detail::setConnectionHandler(c, [this](auto* e) { onChanged(e); });
        }
        else if (event->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                 event->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
        {
            remove(c, "Peer disconnected",
                   event->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally
                       ? AbortReason::PeerTimeout
                       : AbortReason::PeerLeft);
        }
    }

    void broadcast(bool reliable)
    {
        auto snapshot = match.snapshot();
        snapshot.stateSequence = ++stateSequence;
        snapshot.names = names;
        for (const auto& entry : peers)
            if (entry.second.id)
            {
                snapshot.inputAck[entry.second.id - 1] = entry.second.controlSequence;
                snapshot.actionAck[entry.second.id - 1] = entry.second.actionSequence;
            }
        auto e = envelope();
        *e.mutable_snapshot() = encodeSnapshot(snapshot);
        for (const auto& entry : peers)
            if (entry.second.id)
                send(entry.first, e, reliable);
    }

    void receive(HSteamNetConnection c, const wire::Envelope& e)
    {
        auto found = peers.find(c);
        if (found == peers.end())
            return;
        auto& peer = found->second;
        peer.packet = Clock::now();
        if (!peer.hello)
        {
            if (!e.has_hello() || e.hello().owner_token().size() > 128 ||
                e.hello().reservation_token().size() > 64 || !validNickname(e.hello().nickname()))
            {
                remove(c, "Invalid handshake");
                return;
            }
            PlayerId id = 0;
            const auto occupied = [&](PlayerId n)
            {
                return std::any_of(peers.begin(), peers.end(),
                                   [n](const auto& p) { return p.second.id == n; });
            };
            std::string nickname = e.hello().nickname();
            if (admission)
            {
                if (auto accepted = admission(e.hello().reservation_token(), nickname))
                {
                    if (accepted->player >= 1 && accepted->player <= 2 && !occupied(accepted->player))
                    {
                        id = accepted->player;
                        nickname = accepted->nickname;
                    }
                }
            }
            else if (!token.empty())
            {
                if (e.hello().owner_token() == token && !occupied(1))
                    id = 1;
                else if (e.hello().owner_token().empty() && occupied(1) && !occupied(2) &&
                         match.phase() == MatchPhase::Lobby)
                    id = 2;
            }
            else if (match.phase() == MatchPhase::Lobby)
                id = !occupied(1) ? 1 : !occupied(2) ? 2 : 0;
            if (!id)
            {
                remove(c, admission ? "Reservation invalid or expired" : "Room full or match in progress");
                return;
            }
            peer.id = id;
            peer.hello = true;
            names[id - 1] = nickname;
            match.connect(id);
            auto welcome = envelope();
            welcome.mutable_welcome()->set_player_id(id);
            send(c, welcome, true);
            broadcast(true);
        }
        else if (e.has_control())
        {
            const auto& control = e.control();
            if (control.round() != match.round() ||
                (match.phase() != MatchPhase::Round && match.phase() != MatchPhase::RoundEnd))
                return;
            if (control.commands_size() < 1 || control.commands_size() > 16)
            {
                remove(c, "Invalid input batch");
                return;
            }
            for (const auto& command : control.commands())
            {
                if (!command.sequence() || !std::isfinite(command.movement()) ||
                    std::abs(command.movement()) > 1 || !validAim(command.aim()))
                {
                    remove(c, "Invalid input");
                    return;
                }
                if (command.sequence() <= peer.controlSequence ||
                    command.sequence() - peer.controlSequence > 240)
                    continue;
                peer.commands.emplace(command.sequence(), PlayerInput{command.movement(),
                                                                      false,
                                                                      false,
                                                                      {command.aim().x(), command.aim().y()},
                                                                      command.fire_held()});
            }
            peer.input = Clock::now();
        }
        else if (e.has_action())
        {
            const auto& a = e.action();
            if (!validAim(a.aim()) || (!a.jump() && !a.shoot()) || (a.jump() && a.shoot()) || !a.sequence())
            {
                remove(c, "Invalid action");
                return;
            }
            if (a.round() != match.round() ||
                (match.phase() != MatchPhase::Round && match.phase() != MatchPhase::RoundEnd) ||
                a.sequence() <= peer.receivedAction)
                return;
            if (peer.actions.size() >= 240 || a.input_sequence() > peer.controlSequence + 240)
            {
                remove(c, "Too many pending actions");
                return;
            }
            peer.receivedAction = a.sequence();
            peer.actions.push_back(a);
        }
        else if (e.has_weapon_choice())
        {
            const auto& choice = e.weapon_choice();
            match.chooseWeapon(peer.id, choice.reroll(), choice.round(), choice.selection_revision());
            broadcast(true);
        }
        else if (e.has_ready())
        {
            if (e.ready().selection_revision() != match.snapshot().selectionRevision)
                return;
            match.setReady(peer.id, e.ready().ready());
            broadcast(true);
        }
        else if (e.has_map_choice())
        {
            const auto& choice = e.map_choice();
            if (choice.change())
                match.changeMap(peer.id, choice.selection_revision());
            else
                match.selectMap(peer.id, static_cast<MapId>(choice.map()), choice.selection_revision());
            broadcast(true);
        }
        else if (e.has_leave())
            remove(c, "Peer left room");
        else if (!e.has_heartbeat())
            remove(c, "Unexpected message");
    }
};

Server::Server(std::uint16_t port, std::string token, AdmitPlayer admission)
    : impl_(std::make_unique<Impl>(port, std::move(token), std::move(admission)))
{
}

Server::~Server() = default;

void Server::poll()
{
    SteamNetworkingSockets()->RunCallbacks();
    SteamNetworkingMessage_t* messages[128];
    const int count = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(impl_->group, messages, 128);
    for (int i = 0; i < count; ++i)
    {
        auto* m = messages[i];
        wire::Envelope e;
        if (decode(m->m_pData, m->m_cbSize, e))
            impl_->receive(m->m_conn, e);
        else
            impl_->remove(m->m_conn, "Incompatible protocol or malformed message");
        m->Release();
    }
    std::vector<HSteamNetConnection> expired;
    for (const auto& entry : impl_->peers)
    {
        if (age(entry.second.packet) >= 5)
            expired.push_back(entry.first);
        else if (entry.second.id && age(entry.second.input) >= .25f)
        {
            const auto& player = impl_->match.world().players()[entry.second.id - 1];
            impl_->match.setInput(entry.second.id, {0, false, false, player.aimDirection});
        }
    }
    for (auto c : expired)
        impl_->remove(c, "Peer timed out", AbortReason::PeerTimeout);
}

void Server::step(float dt)
{
    const auto before = impl_->match.phase();
    // At most one fixed command per server tick; packet rate cannot speed up physics.
    for (auto& entry : impl_->peers)
    {
        auto& peer = entry.second;
        if (!peer.id)
            continue;
        if (before != MatchPhase::Round && before != MatchPhase::RoundEnd)
        {
            peer.commands.clear();
            peer.actions.clear();
            continue;
        }
        if (!peer.commands.empty())
        {
            auto command = peer.commands.begin();
            if (command->first == peer.controlSequence + 1 || ++peer.gapTicks >= 2)
            {
                impl_->match.setInput(peer.id, command->second);
                peer.controlSequence = command->first;
                peer.commands.erase(command);
                peer.gapTicks = 0;
            }
        }
        while (!peer.actions.empty() && peer.actions.front().input_sequence() <= peer.controlSequence)
        {
            const auto action = peer.actions.front();
            peer.actions.pop_front();
            impl_->match.action(peer.id, action.round(), action.sequence(), action.jump(), action.shoot(),
                                {action.aim().x(), action.aim().y()});
            peer.actionSequence = action.sequence();
        }
    }
    impl_->match.update(dt);
    const auto events = impl_->match.takeEvents();
    if (!events.empty())
    {
        auto e = envelope();
        *e.mutable_events() = encodeEvents(events);
        for (const auto& entry : impl_->peers)
            if (entry.second.id)
                send(entry.first, e, true);
    }
    const auto s = impl_->match.snapshot();
    if (before != s.phase || s.tick % 2 == 0)
        impl_->broadcast(before != s.phase);
}

const Match& Server::match() const
{
    return impl_->match;
}

std::uint16_t Server::port() const
{
    return impl_->port;
}

bool Server::hostLeft() const
{
    return impl_->hostGone;
}

} // namespace cubeshot::network
