#include "network/Client.h"
#include "game/Prediction.h"
#include "network/Address.h"
#include "network/Identity.h"
#include "network/SnapshotInterpolation.h"
#include "network/detail/GnsRuntime.h"
#include <algorithm>
#include <cmath>
#include <steam/isteamnetworkingutils.h>

namespace cubeshot::network
{
using detail::age;
using detail::Clock;
using detail::GnsRuntime;
using detail::options;
using detail::send;

struct Client::Impl
{
    GnsRuntime runtime;
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
    SnapshotInterpolation interpolation;

    void close()
    {
        if (connection)
        {
            detail::removeConnectionHandler(connection);
            SteamNetworkingSockets()->CloseConnection(connection, 1000, "Leaving room", false);
        }
        connection = 0;
        id = 0;
        pending = false;
        prediction.reset();
        predictionTime = 0;
    }

    ~Impl()
    {
        close();
    }

    void onChanged(SteamNetConnectionStatusChangedCallback_t* e)
    {
        if (e->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected)
        {
            auto hello = envelope();
            hello.mutable_hello()->set_owner_token(token);
            hello.mutable_hello()->set_nickname(nickname);
            hello.mutable_hello()->set_reservation_token(reservation);
            send(connection, hello, true);
        }
        else if (e->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                 e->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
        {
            error = e->m_info.m_szEndDebug;
            if (error.empty())
                error = "Connection lost";
            close();
        }
    }
};

Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() = default;

bool Client::connect(const std::string& input, const std::string& token, const std::string& nickname,
                     const std::string& reservation)
{
    disconnect();
    impl_->error.clear();
    impl_->latest.reset();
    impl_->interpolation.clear();
    impl_->actionSequence = 0;
    impl_->lastPrediction = Clock::now();
    if (!validNickname(nickname) || reservation.size() > 64)
    {
        impl_->error = "Invalid nickname or reservation";
        return false;
    }
    impl_->nickname = nickname;
    impl_->reservation = reservation;
    if (!validAddress(input))
    {
        impl_->error = "Invalid IPv4 address or port";
        return false;
    }
    SteamNetworkingIPAddr address;
    address.Clear();
    const std::string target = input.find(':') == std::string::npos ? input + ":27015" : input;
    if (!address.ParseString(target.c_str()))
        return false;
    SteamNetworkingConfigValue_t config[4];
    options(config);
    impl_->connection = SteamNetworkingSockets()->ConnectByIPAddress(address, 4, config);
    if (!impl_->connection)
    {
        impl_->error = "Cannot create connection";
        return false;
    }
    impl_->token = token;
    impl_->pending = true;
    impl_->started = impl_->packet = impl_->nextControl = Clock::now();
    detail::setConnectionHandler(impl_->connection, [this](auto* event) { impl_->onChanged(event); });
    return true;
}

void Client::disconnect()
{
    if (impl_->connection && impl_->id)
    {
        auto e = envelope();
        e.mutable_leave();
        send(impl_->connection, e, true);
    }
    impl_->close();
}

void Client::poll()
{
    SteamNetworkingSockets()->RunCallbacks();
    if (!impl_->connection)
        return;
    SteamNetworkingMessage_t* messages[128];
    const int count = SteamNetworkingSockets()->ReceiveMessagesOnConnection(impl_->connection, messages, 128);
    bool invalid = false;
    for (int i = 0; i < count; ++i)
    {
        wire::Envelope e;
        if (!decode(messages[i]->m_pData, messages[i]->m_cbSize, e))
            invalid = true;
        else if (e.has_welcome())
        {
            if (impl_->id || e.welcome().player_id() < 1 || e.welcome().player_id() > 2)
                invalid = true;
            else
            {
                impl_->id = e.welcome().player_id();
                impl_->pending = false;
                impl_->packet = Clock::now();
            }
        }
        else if (e.has_snapshot())
        {
            auto s = decodeSnapshot(e.snapshot());
            if (!s)
                invalid = true;
            else if (!impl_->latest || s->tick > impl_->latest->tick)
            {
                impl_->packet = Clock::now();
                impl_->latest = *s;
                impl_->prediction.reconcile(*s, impl_->id);
                impl_->interpolation.add(*s, impl_->packet);
            }
        }
        else
            invalid = true;
        messages[i]->Release();
    }
    if (invalid)
    {
        impl_->error = "Invalid server message";
        impl_->close();
    }
    else if ((impl_->pending && age(impl_->started) >= 5) || (!impl_->pending && age(impl_->packet) >= 5))
    {
        impl_->error = "Connection timed out";
        impl_->close();
    }
}

void Client::control(PlayerInput input)
{
    const auto now = Clock::now();
    const double elapsed = std::min(.1, std::chrono::duration<double>(now - impl_->lastPrediction).count());
    impl_->lastPrediction = now;
    if (!connected() || !impl_->latest)
        return;
    impl_->predictionTime += elapsed;
    while (impl_->predictionTime >= 1.0 / 120)
    {
        impl_->prediction.advance(input);
        impl_->predictionTime -= 1.0 / 120;
    }
    if (now < impl_->nextControl)
        return;
    impl_->nextControl =
        now + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 60));
    auto e = envelope();
    const auto& commands = impl_->prediction.commands();
    if (commands.empty() || impl_->latest->phase != MatchPhase::Round)
        e.mutable_heartbeat();
    else
    {
        auto* control = e.mutable_control();
        control->set_round(impl_->latest->round);
        // Redundancy recovers short unreliable losses without retransmission stalls.
        const auto first = commands.size() > 8 ? commands.size() - 8 : 0;
        for (std::size_t i = first; i < commands.size(); ++i)
        {
            const auto& command = commands[i];
            auto* out = control->add_commands();
            out->set_sequence(command.sequence);
            out->set_movement(command.input.moveDirection);
            out->mutable_aim()->set_x(command.input.aimDirection.x);
            out->mutable_aim()->set_y(command.input.aimDirection.y);
        }
    }
    send(impl_->connection, e, false);
}

void Client::action(bool jump, bool shoot, Vec2 aim)
{
    if (!connected() || !impl_->latest || impl_->latest->phase != MatchPhase::Round ||
        !std::isfinite(aim.x) || !std::isfinite(aim.y) || std::hypot(aim.x, aim.y) < .0001f)
        return;
    auto e = envelope();
    auto* a = e.mutable_action();
    a->set_sequence(++impl_->actionSequence);
    a->set_input_sequence(impl_->prediction.sequence() + 1);
    impl_->prediction.action(impl_->actionSequence, jump, shoot, aim);
    a->set_round(impl_->latest->round);
    a->set_jump(jump);
    a->set_shoot(shoot);
    a->mutable_aim()->set_x(aim.x);
    a->mutable_aim()->set_y(aim.y);
    send(impl_->connection, e, true);
}

void Client::ready(bool value)
{
    if (!connected())
        return;
    auto e = envelope();
    e.mutable_ready()->set_ready(value);
    send(impl_->connection, e, true);
}

bool Client::connected() const
{
    return impl_->id != 0;
}

bool Client::connecting() const
{
    return impl_->pending;
}

PlayerId Client::playerId() const
{
    return impl_->id;
}

const std::string& Client::error() const
{
    return impl_->error;
}

const std::optional<WorldSnapshot>& Client::snapshot() const
{
    return impl_->latest;
}

const std::optional<Player>& Client::predictedPlayer() const
{
    return impl_->prediction.player();
}

WorldSnapshot Client::interpolated() const
{
    if (!impl_->latest)
        return {};
    auto out = impl_->interpolation.sample(*impl_->latest, Clock::now());
    if (impl_->prediction.player())
    {
        for (auto& player : out.players)
            if (player.id == impl_->id)
            {
                player = *impl_->prediction.player();
                const auto offset = impl_->prediction.renderOffset();
                player.position.x += offset.x;
                player.position.y += offset.y;
            }
        for (const auto& bullet : impl_->prediction.bullets())
            out.bullets.push_back(bullet);
    }
    return out;
}
} // namespace cubeshot::network
