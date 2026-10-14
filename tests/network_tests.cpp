#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "network/Session.h"
#include <steam/steamnetworkingsockets.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cubeshot;
using namespace cubeshot::network;
namespace {
using Clock = std::chrono::steady_clock;
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void portOwnership() {
    // Keep Winsock initialized while the native socket is alive on Windows.
    Client runtime;
    const auto socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    expect(socket != INVALID_SOCKET, "Create native IPv4 port occupant");
    const auto closeSocket = [&] { closesocket(socket); };
#else
    expect(socket >= 0, "Create native IPv4 port occupant");
    const auto closeSocket = [&] { close(socket); };
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(29721);
    const int bound = bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (bound != 0) { closeSocket(); throw std::runtime_error("Bind native IPv4 port occupant"); }
    bool rejected = false;
    try { Server second(29721); }
    catch (const std::runtime_error&) { rejected = true; }
    closeSocket();
    expect(rejected, "Server rejects port occupied by native IPv4 socket");

    Server first(29722);
    rejected = false;
    try { Server second(29722); }
    catch (const std::runtime_error&) { rejected = true; }
    expect(rejected, "Two servers cannot listen on the same game port");
}
struct Rig {
    Server server{29720, "test-owner"};
    Client host, guest;
    PlayerInput hostInput{}, guestInput{};
    bool feedGuest = true;
    Clock::time_point previous = Clock::now();
    double accumulator = 0;
    std::string address() const { return "127.0.0.1:" + std::to_string(server.port()); }
    void tick() {
        server.poll(); host.poll(); guest.poll(); host.control(hostInput);
        if (feedGuest) guest.control(guestInput);
        const auto now = Clock::now();
        accumulator += std::chrono::duration<double>(now - previous).count(); previous = now;
        while (accumulator >= 1.0 / 120) { server.step(); accumulator -= 1.0 / 120; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    template<class F> void until(F condition, double timeout, const char* message) {
        const auto deadline = Clock::now() + std::chrono::duration<double>(timeout);
        while (!condition() && Clock::now() < deadline) tick();
        expect(condition(), message);
    }
    void run(double seconds) {
        const auto deadline = Clock::now() + std::chrono::duration<double>(seconds);
        while (Clock::now() < deadline) tick();
    }
};
void connectionAndTimeout() {
    Rig r;
    r.guest.connect(r.address());
    r.until([&] { return !r.guest.connecting(); }, 3, "Reserved owner slot rejects guest before owner joins");
    expect(!r.guest.connected(), "Guest cannot become host");
    r.host.connect(r.address(), "test-owner");
    r.until([&] { return r.host.connected() && r.host.snapshot(); }, 3, "Owner connects");
    r.guest.connect(r.address());
    r.until([&] { return r.guest.connected() && r.guest.snapshot(); }, 3, "Guest connects");
    expect(r.host.playerId() == 1 && r.guest.playerId() == 2, "Server assigns trusted slots");
    Client third; third.connect(r.address());
    const auto thirdDeadline = Clock::now() + std::chrono::seconds(3);
    while (third.connecting() && Clock::now() < thirdDeadline) { r.tick(); third.poll(); }
    expect(!third.connected() && !third.connecting(), "Reject third player");
    // A real connection with incompatible Protobuf handshake.
    SteamNetworkingIPAddr ip; ip.Clear(); ip.SetIPv4(0x7f000001, r.server.port());
    SteamNetworkingConfigValue_t config;
    config.SetInt32(k_ESteamNetworkingConfig_IP_AllowWithoutAuth, 1);
    const auto raw = SteamNetworkingSockets()->ConnectByIPAddress(ip, 1, &config);
    expect(raw != 0, "Raw protocol test connection");
    bool sent = false, rejected = false;
    const auto rawDeadline = Clock::now() + std::chrono::seconds(3);
    while (!rejected && Clock::now() < rawDeadline) {
        r.tick(); SteamNetConnectionInfo_t info{};
        SteamNetworkingSockets()->GetConnectionInfo(raw, &info);
        if (info.m_eState == k_ESteamNetworkingConnectionState_Connected && !sent) {
            auto e = envelope(); e.set_protocol(999); e.mutable_hello(); const auto bytes = e.SerializeAsString();
            SteamNetworkingSockets()->SendMessageToConnection(raw, bytes.data(), bytes.size(), k_nSteamNetworkingSend_ReliableNoNagle, nullptr);
            sent = true;
        }
        rejected = info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer;
    }
    SteamNetworkingSockets()->CloseConnection(raw, 0, nullptr, false);
    expect(sent && rejected, "Incompatible protocol rejected by server");
    r.host.ready(true); r.guest.ready(true);
    r.until([&] { return r.server.match().phase() == MatchPhase::Round; }, 4, "Lobby countdown reaches round");
    r.guestInput.moveDirection = -1; r.run(.15);
    expect(r.server.match().world().players()[1].velocity.x < 0, "Remote movement applied");
    r.feedGuest = false; r.run(.4);
    expect(r.server.match().world().players()[1].velocity.x == 0, "Lost continuous input stops after 250ms");
    r.until([&] { return !r.server.match().snapshot().connected[1]; }, 5.5, "Silent client times out");
    const auto stopped = r.server.match().snapshot();
    expect(stopped.phase == MatchPhase::Lobby && stopped.score[0] == 0 && stopped.score[1] == 0 &&
           !stopped.ready[0] && stopped.aborted == AbortReason::PeerTimeout, "Timeout aborts and clears score/readiness");
    r.guest.disconnect(); r.feedGuest = true; r.guestInput = {};
    r.guest.connect(r.address()); r.until([&] { return r.guest.connected(); }, 3, "Replacement guest joins same host room");
    r.host.disconnect(); r.until([&] { return r.server.hostLeft(); }, 3, "Owner leave marks server for shutdown");
}
void impairedCombat() {
    Rig r; simulateNetwork(5, 100);
    r.host.connect(r.address(), "test-owner");
    r.until([&] { return r.host.connected() && r.host.snapshot(); }, 5, "Owner connects under loss");
    r.guest.connect(r.address());
    r.until([&] { return r.guest.connected() && r.guest.snapshot(); }, 5, "Guest connects under loss");
    r.host.ready(true); r.guest.ready(true);
    r.until([&] { return r.host.snapshot()->phase == MatchPhase::Round && r.guest.snapshot()->phase == MatchPhase::Round; }, 5, "Reliable ready starts impaired match");
    r.hostInput.moveDirection = 1; r.guestInput.moveDirection = -1; r.run(1);
    r.hostInput = {}; r.guestInput = {}; r.run(.35);
    for (int i = 0; i < 5; ++i) {
        const auto from = r.server.match().world().players()[0].weaponOrigin();
        const auto to = r.server.match().world().players()[1].weaponOrigin();
        r.host.action(false, true, {to.x - from.x, to.y - from.y});
        r.until([&] {
            const auto target = 100 - (i + 1) * 20;
            return r.host.snapshot()->players[1].health == target && r.guest.snapshot()->players[1].health == target;
        }, 3, "Reliable shot reaches both clients under packet loss");
        r.run(.3); // Следующий клик после подтверждённого попадания и кулдауна.
    }
    r.until([&] { return r.host.snapshot()->score[0] == 1 && r.guest.snapshot()->score[0] == 1; }, 2, "Both clients agree on authoritative score under 5% loss and 100ms lag");
    expect(r.host.snapshot()->score[1] == 0 && r.guest.snapshot()->score[1] == 0, "Exactly one round point");
    expect(r.server.match().world().players()[1].health == 0, "Exactly five reliable shots kill remote player");
    r.guest.disconnect();
    r.until([&] { return r.host.snapshot()->phase == MatchPhase::Lobby && r.host.snapshot()->score[0] == 0; }, 3, "Leaving game resets score without award");
    simulateNetwork(0, 0);
}
}
int main() {
    try { portOwnership(); connectionAndTimeout(); impairedCombat(); std::cout << "Real UDP transport and impaired combat passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
