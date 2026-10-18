#include "network/Discovery.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <random>
#include <set>

namespace cubeshot::network
{
namespace
{
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalidSocket = INVALID_SOCKET;

void closeSocket(Socket s)
{
    closesocket(s);
}
#else
using Socket = int;
constexpr Socket invalidSocket = -1;

void closeSocket(Socket s)
{
    close(s);
}
#endif
using Clock = std::chrono::steady_clock;

std::set<std::uint32_t> broadcasts()
{
    std::set<std::uint32_t> out{htonl(INADDR_BROADCAST), htonl(INADDR_LOOPBACK)};
#ifdef _WIN32
    ULONG size = 16384;
    std::vector<unsigned char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    auto result = GetAdaptersAddresses(
        AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
        adapters, &size);
    if (result == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(size);
        adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        result = GetAdaptersAddresses(
            AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr,
            adapters, &size);
    }
    if (result == NO_ERROR)
        for (auto* a = adapters; a; a = a->Next)
        {
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;
            for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
            {
                if (u->Address.lpSockaddr->sa_family != AF_INET || u->OnLinkPrefixLength > 30)
                    continue;
                const auto ip = ntohl(reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr);
                const std::uint32_t mask =
                    u->OnLinkPrefixLength ? 0xffffffffu << (32 - u->OnLinkPrefixLength) : 0;
                out.insert(htonl(ip | ~mask));
            }
        }
#else
    ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) == 0)
    {
        for (auto* a = interfaces; a; a = a->ifa_next)
        {
            if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET && a->ifa_broadaddr &&
                (a->ifa_flags & IFF_UP) && (a->ifa_flags & IFF_BROADCAST))
                out.insert(reinterpret_cast<sockaddr_in*>(a->ifa_broadaddr)->sin_addr.s_addr);
        }
        freeifaddrs(interfaces);
    }
#endif
    return out;
}
} // namespace

struct Discovery::Impl
{
    Socket socket = invalidSocket;
    bool advertiser;
    std::uint16_t discovery, game = gamePort;
    unsigned players = 0;
    bool joinable = false;
    std::uint64_t nonce =
        (static_cast<std::uint64_t>(std::random_device{}()) << 32) | std::random_device{}() | 1;
    Clock::time_point lastQuery{};
    std::map<std::string, std::pair<Room, Clock::time_point>> found;
#ifdef _WIN32
    bool winsock = false;
#endif
    Impl(bool ad, std::uint16_t port) : advertiser(ad), discovery(port)
    {
#ifdef _WIN32
        WSADATA data;
        winsock = WSAStartup(MAKEWORD(2, 2), &data) == 0;
        if (!winsock)
            return;
#endif
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == invalidSocket)
            return;
        int yes = 1;
        setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof(yes));
        if (ad)
            setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(ad ? port : 0);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
        {
            closeSocket(socket);
            socket = invalidSocket;
            return;
        }
#ifdef _WIN32
        u_long nonblocking = 1;
        ioctlsocket(socket, FIONBIO, &nonblocking);
#else
        fcntl(socket, F_SETFL, fcntl(socket, F_GETFL, 0) | O_NONBLOCK);
#endif
    }

    ~Impl()
    {
        if (socket != invalidSocket)
            closeSocket(socket);
#ifdef _WIN32
        if (winsock)
            WSACleanup();
#endif
    }

    void query()
    {
        wire::DiscoveryQuery message;
        message.set_protocol(protocolVersion);
        message.set_nonce(nonce);
        const std::string bytes = "CSQ1" + message.SerializeAsString();
        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(discovery);
        for (auto ip : broadcasts())
        {
            target.sin_addr.s_addr = ip;
            sendto(socket, bytes.data(), static_cast<int>(bytes.size()), 0,
                   reinterpret_cast<sockaddr*>(&target), sizeof(target));
        }
        lastQuery = Clock::now();
    }
};

Discovery::Discovery(bool ad, std::uint16_t port) : impl_(std::make_unique<Impl>(ad, port)) {}

Discovery::~Discovery() = default;

bool Discovery::available() const
{
    return impl_->socket != invalidSocket;
}

void Discovery::advertise(std::uint16_t port, unsigned players, bool joinable)
{
    impl_->game = port;
    impl_->players = players;
    impl_->joinable = joinable;
}

void Discovery::poll()
{
    if (!available())
        return;
    const auto now = Clock::now();
    if (!impl_->advertiser && now - impl_->lastQuery >= std::chrono::seconds(2))
        impl_->query();
    char bytes[513];
    for (int budget = 0; budget < 64; ++budget)
    {
        sockaddr_in source{};
#ifdef _WIN32
        int size = sizeof(source);
#else
        socklen_t size = sizeof(source);
#endif
        const int count = static_cast<int>(
            recvfrom(impl_->socket, bytes, sizeof(bytes), 0, reinterpret_cast<sockaddr*>(&source), &size));
        if (count < 0)
            break;
        if (count < 5 || count > 512)
            continue;
        if (impl_->advertiser && std::memcmp(bytes, "CSQ1", 4) == 0)
        {
            wire::DiscoveryQuery query;
            if (!query.ParseFromArray(bytes + 4, count - 4) || query.protocol() != protocolVersion ||
                !query.nonce())
                continue;
            wire::DiscoveryRoom reply;
            reply.set_protocol(protocolVersion);
            reply.set_nonce(query.nonce());
            reply.set_port(impl_->game);
            reply.set_players(impl_->players);
            reply.set_joinable(impl_->joinable);
            reply.set_name("CubeShot LAN");
            const std::string output = "CSR1" + reply.SerializeAsString();
            sendto(impl_->socket, output.data(), static_cast<int>(output.size()), 0,
                   reinterpret_cast<sockaddr*>(&source), size);
        }
        else if (!impl_->advertiser && std::memcmp(bytes, "CSR1", 4) == 0)
        {
            wire::DiscoveryRoom room;
            if (!room.ParseFromArray(bytes + 4, count - 4) || room.protocol() != protocolVersion ||
                room.nonce() != impl_->nonce || !room.port() || room.port() > 65535 || room.players() > 2 ||
                room.name().size() > 32)
                continue;
            char ip[INET_ADDRSTRLEN];
            if (!inet_ntop(AF_INET, &source.sin_addr, ip, sizeof(ip)))
                continue;
            const std::string address = std::string(ip) + ':' + std::to_string(room.port());
            // Список комнаты на одном ПК может содержать loopback и адрес LAN; это
            // разные пригодные пути подключения, их названия явно содержат IP.
            impl_->found[address] = {{address, room.name(), room.players(), room.joinable()}, now};
        }
    }
    for (auto it = impl_->found.begin(); it != impl_->found.end();)
    {
        if (now - it->second.second >= std::chrono::seconds(6))
            it = impl_->found.erase(it);
        else
            ++it;
    }
}

std::vector<Room> Discovery::rooms() const
{
    std::vector<Room> out;
    for (const auto& room : impl_->found)
        out.push_back(room.second.first);
    return out;
}
} // namespace cubeshot::network
