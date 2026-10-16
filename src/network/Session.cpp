#include "network/Session.h"
#include "game/Prediction.h"
#include "network/Identity.h"
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <stdexcept>

namespace cubeshot::network {
namespace {
using Clock = std::chrono::steady_clock;
using Handler = std::function<void(SteamNetConnectionStatusChangedCallback_t*)>;
std::map<HSteamNetConnection, Handler> connections;
std::map<HSteamListenSocket, Handler> listeners;
unsigned users = 0;
void changed(SteamNetConnectionStatusChangedCallback_t* event)
{
    auto found = connections.find(event->m_hConn);
    if (found != connections.end()) { auto handler = found->second; handler(event); return; }
    auto listener = listeners.find(event->m_info.m_hListenSocket);
    if (listener != listeners.end()) { auto handler = listener->second; handler(event); }
}
struct Runtime {
    Runtime() {
        if (users == 0) {
            SteamDatagramErrMsg error;
            if (!GameNetworkingSockets_Init(nullptr, error)) throw std::runtime_error(error);
        }
        ++users;
    }
    ~Runtime() { if (--users == 0) GameNetworkingSockets_Kill(); }
};
void options(SteamNetworkingConfigValue_t (&out)[4])
{
    out[0].SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(changed));
    out[1].SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
    out[2].SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, 5000);
    out[3].SetInt32(k_ESteamNetworkingConfig_TimeoutConnected, 5000);
}
bool send(HSteamNetConnection connection, const wire::Envelope& message, bool reliable)
{
    const std::string bytes = message.SerializeAsString();
    return SteamNetworkingSockets()->SendMessageToConnection(connection, bytes.data(), static_cast<uint32>(bytes.size()),
        reliable ? k_nSteamNetworkingSend_ReliableNoNagle : k_nSteamNetworkingSend_UnreliableNoNagle, nullptr) == k_EResultOK;
}
float age(Clock::time_point time) { return std::chrono::duration<float>(Clock::now() - time).count(); }
}

bool validAddress(const std::string& address)
{
    // LAN v1: только IPv4 и необязательный порт. Никакого DNS/IPv6 в форме.
    const auto colon = address.find(':');
    if (colon != std::string::npos && address.find(':', colon + 1) != std::string::npos) return false;
    const std::string ip = address.substr(0, colon);
    unsigned octets = 0, value = 0, digits = 0;
    for (char c : ip + '.') {
        if (c == '.') {
            if (!digits || value > 255) return false;
            ++octets; value = digits = 0;
        } else if (c >= '0' && c <= '9' && digits < 3) { value = value * 10 + c - '0'; ++digits; }
        else return false;
    }
    if (octets != 4) return false;
    if (colon != std::string::npos) {
        auto port = address.substr(colon + 1);
        if (port.empty() || port.size() > 5) return false;
        unsigned n = 0;
        for (char c : port) { if (c < '0' || c > '9') return false; n = n * 10 + c - '0'; }
        if (!n || n > 65535) return false;
    }
    return true;
}

struct Server::Impl {
    Runtime runtime;
    HSteamListenSocket listener = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup group = k_HSteamNetPollGroup_Invalid;
    struct Peer {
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
    explicit Impl(std::uint16_t requested, std::string owner, AdmitPlayer admit) : token(std::move(owner)), admission(std::move(admit)) {
        // LAN v1 uses IPv4. An unspecified IPv6 address lets GNS retry IPv4
        // after a failed bind; on Windows that can hide an occupied dual-stack port.
        SteamNetworkingIPAddr address; address.SetIPv4(0, requested);
        SteamNetworkingConfigValue_t config[4]; options(config);
        group = SteamNetworkingSockets()->CreatePollGroup();
        listener = SteamNetworkingSockets()->CreateListenSocketIP(address, 4, config);
        if (!group || !listener) {
            if (group) SteamNetworkingSockets()->DestroyPollGroup(group);
            if (listener) SteamNetworkingSockets()->CloseListenSocket(listener);
            throw std::runtime_error("Cannot bind game port (already in use?)");
        }
        SteamNetworkingSockets()->GetListenSocketAddress(listener, &address);
        port = address.m_port;
        listeners[listener] = [this](auto* event) { onChanged(event); };
    }
    ~Impl() {
        listeners.erase(listener);
        for (const auto& entry : peers) {
            connections.erase(entry.first);
            SteamNetworkingSockets()->CloseConnection(entry.first, 1000, "Room closed by host", false);
        }
        SteamNetworkingSockets()->CloseListenSocket(listener);
        SteamNetworkingSockets()->DestroyPollGroup(group);
    }
    void remove(HSteamNetConnection c, const char* reason, AbortReason abort = AbortReason::PeerLeft) {
        auto found = peers.find(c);
        if (found == peers.end()) return;
        const auto id = found->second.id;
        if (id) {
            match.disconnect(id, abort);
            names[id - 1] = "Игрок " + std::to_string(id);
            if (!token.empty() && id == 1) hostGone = true;
        }
        connections.erase(c);
        SteamNetworkingSockets()->CloseConnection(c, 1000, reason, false);
        peers.erase(found);
        broadcast(true);
    }
    void onChanged(SteamNetConnectionStatusChangedCallback_t* event) {
        const auto c = event->m_hConn;
        if (event->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting) {
            if (peers.size() >= 4 || hostGone || SteamNetworkingSockets()->AcceptConnection(c) != k_EResultOK) {
                SteamNetworkingSockets()->CloseConnection(c, 1000, "Room full", false); return;
            }
            SteamNetworkingSockets()->SetConnectionPollGroup(c, group);
            peers.emplace(c, Peer{});
            connections[c] = [this](auto* e) { onChanged(e); };
        } else if (event->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                   event->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
            remove(c, "Peer disconnected", event->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally
                   ? AbortReason::PeerTimeout : AbortReason::PeerLeft);
        }
    }
    void broadcast(bool reliable) {
        auto snapshot = match.snapshot(); snapshot.names = names;
        for (const auto& entry : peers) if (entry.second.id) {
            snapshot.inputAck[entry.second.id - 1] = entry.second.controlSequence;
            snapshot.actionAck[entry.second.id - 1] = entry.second.actionSequence;
        }
        auto e = envelope(); *e.mutable_snapshot() = encodeSnapshot(snapshot);
        for (const auto& entry : peers) if (entry.second.id) send(entry.first, e, reliable);
    }
    void receive(HSteamNetConnection c, const wire::Envelope& e) {
        auto found = peers.find(c);
        if (found == peers.end()) return;
        auto& peer = found->second;
        peer.packet = Clock::now();
        if (!peer.hello) {
            if (!e.has_hello() || e.hello().owner_token().size() > 128 || e.hello().reservation_token().size() > 64 || !validNickname(e.hello().nickname())) { remove(c, "Invalid handshake"); return; }
            PlayerId id = 0;
            const auto occupied = [&](PlayerId n) {
                return std::any_of(peers.begin(), peers.end(), [n](const auto& p) { return p.second.id == n; });
            };
            std::string nickname = e.hello().nickname();
            if (admission) {
                if (auto accepted = admission(e.hello().reservation_token(), nickname)) {
                    if (accepted->player >= 1 && accepted->player <= 2 && !occupied(accepted->player)) {
                        id = accepted->player; nickname = accepted->nickname;
                    }
                }
            } else if (!token.empty()) {
                if (e.hello().owner_token() == token && !occupied(1)) id = 1;
                else if (e.hello().owner_token().empty() && occupied(1) && !occupied(2) && match.phase() == MatchPhase::Lobby) id = 2;
            } else if (match.phase() == MatchPhase::Lobby) id = !occupied(1) ? 1 : !occupied(2) ? 2 : 0;
            if (!id) { remove(c, admission ? "Reservation invalid or expired" : "Room full or match in progress"); return; }
            peer.id = id; peer.hello = true; names[id - 1] = nickname; match.connect(id);
            auto welcome = envelope(); welcome.mutable_welcome()->set_player_id(id); send(c, welcome, true);
            broadcast(true);
        } else if (e.has_control()) {
            const auto& control = e.control();
            if (control.round() != match.round() || match.phase() != MatchPhase::Round) return;
            if (control.commands_size() < 1 || control.commands_size() > 16) { remove(c, "Invalid input batch"); return; }
            for (const auto& command : control.commands()) {
                if (!command.sequence() || !std::isfinite(command.movement()) || std::abs(command.movement()) > 1 || !validAim(command.aim())) {
                    remove(c, "Invalid input"); return;
                }
                if (command.sequence() <= peer.controlSequence || command.sequence() - peer.controlSequence > 240) continue;
                peer.commands.emplace(command.sequence(), PlayerInput{command.movement(), false, false, {command.aim().x(), command.aim().y()}});
            }
            peer.input = Clock::now();
        } else if (e.has_action()) {
            const auto& a = e.action();
            if (!validAim(a.aim()) || (!a.jump() && !a.shoot()) || (a.jump() && a.shoot()) || !a.sequence()) { remove(c, "Invalid action"); return; }
            if (a.round() != match.round() || match.phase() != MatchPhase::Round || a.sequence() <= peer.receivedAction) return;
            if (peer.actions.size() >= 240 || a.input_sequence() > peer.controlSequence + 240) { remove(c, "Too many pending actions"); return; }
            peer.receivedAction = a.sequence(); peer.actions.push_back(a);
        } else if (e.has_ready()) {
            match.setReady(peer.id, e.ready().ready()); broadcast(true);
        } else if (e.has_leave()) remove(c, "Peer left room");
        else if (!e.has_heartbeat()) remove(c, "Unexpected message");
    }
};
Server::Server(std::uint16_t port, std::string token, AdmitPlayer admission) : impl_(std::make_unique<Impl>(port, std::move(token), std::move(admission))) {}
Server::~Server() = default;
void Server::poll() {
    SteamNetworkingSockets()->RunCallbacks();
    SteamNetworkingMessage_t* messages[128];
    const int count = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(impl_->group, messages, 128);
    for (int i = 0; i < count; ++i) {
        auto* m = messages[i]; wire::Envelope e;
        if (decode(m->m_pData, m->m_cbSize, e)) impl_->receive(m->m_conn, e);
        else impl_->remove(m->m_conn, "Incompatible protocol or malformed message");
        m->Release();
    }
    std::vector<HSteamNetConnection> expired;
    for (const auto& entry : impl_->peers) {
        if (age(entry.second.packet) >= 5) expired.push_back(entry.first);
        else if (entry.second.id && age(entry.second.input) >= .25f) {
            const auto& player = impl_->match.world().players()[entry.second.id - 1];
            impl_->match.setInput(entry.second.id, {0, false, false, player.aimDirection});
        }
    }
    for (auto c : expired) impl_->remove(c, "Peer timed out", AbortReason::PeerTimeout);
}
void Server::step(float dt) {
    const auto before = impl_->match.phase();
    // At most one fixed command per server tick; packet rate cannot speed up physics.
    for (auto& entry : impl_->peers) {
        auto& peer = entry.second;
        if (!peer.id) continue;
        if (before != MatchPhase::Round) { peer.commands.clear(); peer.actions.clear(); continue; }
        if (!peer.commands.empty()) {
            auto command = peer.commands.begin();
            if (command->first == peer.controlSequence + 1 || ++peer.gapTicks >= 2) {
                impl_->match.setInput(peer.id, command->second);
                peer.controlSequence = command->first; peer.commands.erase(command); peer.gapTicks = 0;
            }
        }
        while (!peer.actions.empty() && peer.actions.front().input_sequence() <= peer.controlSequence) {
            const auto action = peer.actions.front(); peer.actions.pop_front();
            impl_->match.action(peer.id, action.round(), action.sequence(), action.jump(), action.shoot(), {action.aim().x(), action.aim().y()});
            peer.actionSequence = action.sequence();
        }
    }
    impl_->match.update(dt);
    const auto s = impl_->match.snapshot();
    if (before != s.phase || s.tick % 2 == 0) impl_->broadcast(before != s.phase);
}
const Match& Server::match() const { return impl_->match; }
std::uint16_t Server::port() const { return impl_->port; }
bool Server::hostLeft() const { return impl_->hostGone; }

struct Client::Impl {
    Runtime runtime;
    HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
    PlayerId id = 0;
    bool pending = false;
    std::string error, token, nickname, reservation;
    Clock::time_point started, packet, nextControl{};
    std::uint64_t actionSequence = 0;
    Prediction prediction;
    Clock::time_point lastPrediction = Clock::now();
    double predictionTime = 0;
    std::optional<WorldSnapshot> latest;
    std::deque<std::pair<Clock::time_point, WorldSnapshot>> history;
    void close() {
        if (connection) {
            connections.erase(connection);
            SteamNetworkingSockets()->CloseConnection(connection, 1000, "Leaving room", false);
        }
        connection = 0; id = 0; pending = false; prediction.reset(); predictionTime = 0;
    }
    ~Impl() { close(); }
    void onChanged(SteamNetConnectionStatusChangedCallback_t* e) {
        if (e->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
            auto hello = envelope(); hello.mutable_hello()->set_owner_token(token);
            hello.mutable_hello()->set_nickname(nickname); hello.mutable_hello()->set_reservation_token(reservation); send(connection, hello, true);
        } else if (e->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                   e->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
            error = e->m_info.m_szEndDebug; if (error.empty()) error = "Connection lost"; close();
        }
    }
};
Client::Client() : impl_(std::make_unique<Impl>()) {}
Client::~Client() = default;
bool Client::connect(const std::string& input, const std::string& token, const std::string& nickname, const std::string& reservation) {
    disconnect(); impl_->error.clear(); impl_->latest.reset(); impl_->history.clear();
    impl_->actionSequence = 0; impl_->lastPrediction = Clock::now();
    if (!validNickname(nickname) || reservation.size() > 64) { impl_->error = "Invalid nickname or reservation"; return false; }
    impl_->nickname = nickname; impl_->reservation = reservation;
    if (!validAddress(input)) { impl_->error = "Invalid IPv4 address or port"; return false; }
    SteamNetworkingIPAddr address; address.Clear();
    const std::string target = input.find(':') == std::string::npos ? input + ":27015" : input;
    if (!address.ParseString(target.c_str())) return false;
    SteamNetworkingConfigValue_t config[4]; options(config);
    impl_->connection = SteamNetworkingSockets()->ConnectByIPAddress(address, 4, config);
    if (!impl_->connection) { impl_->error = "Cannot create connection"; return false; }
    impl_->token = token; impl_->pending = true; impl_->started = impl_->packet = impl_->nextControl = Clock::now();
    connections[impl_->connection] = [this](auto* event) { impl_->onChanged(event); };
    return true;
}
void Client::disconnect() {
    if (impl_->connection && impl_->id) { auto e = envelope(); e.mutable_leave(); send(impl_->connection, e, true); }
    impl_->close();
}
void Client::poll() {
    SteamNetworkingSockets()->RunCallbacks();
    if (!impl_->connection) return;
    SteamNetworkingMessage_t* messages[128];
    const int count = SteamNetworkingSockets()->ReceiveMessagesOnConnection(impl_->connection, messages, 128);
    bool invalid = false;
    for (int i = 0; i < count; ++i) {
        wire::Envelope e;
        if (!decode(messages[i]->m_pData, messages[i]->m_cbSize, e)) invalid = true;
        else if (e.has_welcome()) {
            if (impl_->id || e.welcome().player_id() < 1 || e.welcome().player_id() > 2) invalid = true;
            else { impl_->id = e.welcome().player_id(); impl_->pending = false; impl_->packet = Clock::now(); }
        } else if (e.has_snapshot()) {
            auto s = decodeSnapshot(e.snapshot());
            if (!s) invalid = true;
            else if (!impl_->latest || s->tick > impl_->latest->tick) {
                impl_->packet = Clock::now(); impl_->latest = *s;
                impl_->prediction.reconcile(*s, impl_->id);
                impl_->history.emplace_back(impl_->packet, *s);
                while (impl_->history.size() > 12) impl_->history.pop_front();
            }
        } else invalid = true;
        messages[i]->Release();
    }
    if (invalid) { impl_->error = "Invalid server message"; impl_->close(); }
    else if ((impl_->pending && age(impl_->started) >= 5) || (!impl_->pending && age(impl_->packet) >= 5)) {
        impl_->error = "Connection timed out"; impl_->close();
    }
}
void Client::control(PlayerInput input) {
    const auto now = Clock::now();
    const double elapsed = std::min(.1, std::chrono::duration<double>(now - impl_->lastPrediction).count());
    impl_->lastPrediction = now;
    if (!connected() || !impl_->latest) return;
    impl_->predictionTime += elapsed;
    while (impl_->predictionTime >= 1.0 / 120) {
        impl_->prediction.advance(input); impl_->predictionTime -= 1.0 / 120;
    }
    if (now < impl_->nextControl) return;
    impl_->nextControl = now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 60));
    auto e = envelope();
    const auto& commands = impl_->prediction.commands();
    if (commands.empty() || impl_->latest->phase != MatchPhase::Round) e.mutable_heartbeat();
    else {
        auto* control = e.mutable_control(); control->set_round(impl_->latest->round);
        // Redundancy recovers short unreliable losses without retransmission stalls.
        const auto first = commands.size() > 8 ? commands.size() - 8 : 0;
        for (std::size_t i = first; i < commands.size(); ++i) {
            const auto& command = commands[i]; auto* out = control->add_commands();
            out->set_sequence(command.sequence); out->set_movement(command.input.moveDirection);
            out->mutable_aim()->set_x(command.input.aimDirection.x); out->mutable_aim()->set_y(command.input.aimDirection.y);
        }
    }
    send(impl_->connection, e, false);
}
void Client::action(bool jump, bool shoot, Vec2 aim) {
    if (!connected() || !impl_->latest || impl_->latest->phase != MatchPhase::Round || !std::isfinite(aim.x) || !std::isfinite(aim.y) || std::hypot(aim.x, aim.y) < .0001f) return;
    auto e = envelope(); auto* a = e.mutable_action(); a->set_sequence(++impl_->actionSequence);
    a->set_input_sequence(impl_->prediction.sequence() + 1);
    impl_->prediction.action(impl_->actionSequence, jump, shoot, aim);
    a->set_round(impl_->latest->round); a->set_jump(jump); a->set_shoot(shoot);
    a->mutable_aim()->set_x(aim.x); a->mutable_aim()->set_y(aim.y); send(impl_->connection, e, true);
}
void Client::ready(bool value) {
    if (!connected()) return;
    auto e = envelope(); e.mutable_ready()->set_ready(value); send(impl_->connection, e, true);
}
bool Client::connected() const { return impl_->id != 0; }
bool Client::connecting() const { return impl_->pending; }
PlayerId Client::playerId() const { return impl_->id; }
const std::string& Client::error() const { return impl_->error; }
const std::optional<WorldSnapshot>& Client::snapshot() const { return impl_->latest; }
const std::optional<Player>& Client::predictedPlayer() const { return impl_->prediction.player(); }
WorldSnapshot Client::interpolated() const {
    if (!impl_->latest) return {};
    auto out = *impl_->latest;
    const auto target = Clock::now() - std::chrono::milliseconds(33);
    for (std::size_t i = 1; i < impl_->history.size(); ++i) {
        const auto& a = impl_->history[i - 1]; const auto& b = impl_->history[i];
        if (target < a.first || target > b.first || a.second.round != b.second.round ||
            a.second.round != out.round || a.second.phase != out.phase || b.second.phase != out.phase) continue;
        const float t = std::chrono::duration<float>(target - a.first).count() /
                        std::chrono::duration<float>(b.first - a.first).count();
        auto lerp = [t](Vec2 x, Vec2 y) { return Vec2{x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t}; };
        for (auto& p : out.players) {
            auto from = std::find_if(a.second.players.begin(), a.second.players.end(), [&](const auto& v) { return v.id == p.id; });
            auto to = std::find_if(b.second.players.begin(), b.second.players.end(), [&](const auto& v) { return v.id == p.id; });
            if (from != a.second.players.end() && to != b.second.players.end() && p.alive()) p.position = lerp(from->position, to->position);
        }
        for (auto& bullet : out.bullets) {
            auto from = std::find_if(a.second.bullets.begin(), a.second.bullets.end(), [&](const auto& v) { return v.id == bullet.id; });
            auto to = std::find_if(b.second.bullets.begin(), b.second.bullets.end(), [&](const auto& v) { return v.id == bullet.id; });
            if (from != a.second.bullets.end() && to != b.second.bullets.end()) bullet.position = lerp(from->position, to->position);
        }
        break;
    }
    if (impl_->prediction.player()) {
        for (auto& player : out.players) if (player.id == impl_->id) {
            player = *impl_->prediction.player();
            const auto offset = impl_->prediction.renderOffset(); player.position.x += offset.x; player.position.y += offset.y;
        }
        for (const auto& bullet : impl_->prediction.bullets()) out.bullets.push_back(bullet);
    }
    return out;
}
void simulateNetwork(float loss, int lag, int jitter) {
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Avg, jitter / 2.f);
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Max, static_cast<float>(jitter));
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Pct, jitter ? 100.f : 0.f);
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, loss);
    SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send, lag);
}
}
