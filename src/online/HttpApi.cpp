#include "online/HttpApi.h"
#include "network/Identity.h"
#include "network/Protocol.h"
#include "online/ArenaPool.h"
#include "online/Reservations.h"
#include "online/Service.h"
#include <algorithm>
#include <deque>
#include <future>
#include <httplib.h>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

namespace cubeshot::online
{
namespace
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

struct Reply
{
    int status;
    Json body;
};

Reply error(int status, const char* code)
{
    return {status, {{"error", code}}};
}

bool hexToken(const std::string& token)
{
    return token.size() == 64 && token.find_first_not_of("0123456789abcdef") == std::string::npos;
}
} // namespace

struct HttpApi::Impl
{
    struct Task
    {
        std::string method, path, body, ip;
        std::promise<Reply> reply;
    };

    ServiceConfig config;
    const ArenaPool& pool;
    Reservations& reservations;
    std::map<std::string, std::pair<Clock::time_point, unsigned>> limits;
    httplib::Server http;
    std::thread httpThread;
    std::mutex mutex;
    std::deque<std::shared_ptr<Task>> tasks;
    std::uint16_t boundPort = 0;

    Impl(ServiceConfig value, const ArenaPool& arenaPool, Reservations& book)
        : config(std::move(value)), pool(arenaPool), reservations(book)
    {
        http.new_task_queue = [] { return new httplib::ThreadPool(2, 2, 32); };
        http.set_payload_max_length(4096).set_read_timeout(3).set_write_timeout(3).set_keep_alive_max_count(
            4);
        // Preserve exclusive port ownership on Windows and do not enable SO_REUSEPORT on Linux.
        http.set_socket_options(
            [](socket_t socket)
            {
#ifdef _WIN32
                int exclusive = 1;
                setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive),
                           sizeof(exclusive));
#else
                int reuse = 1;
                setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
            });
        const auto handler = [this](const httplib::Request& request, httplib::Response& response)
        {
            auto task = std::make_shared<Task>();
            task->method = request.method;
            task->path = request.path;
            task->body = request.body;
            task->ip = request.remote_addr;
            if (config.trustProxy && request.has_header("X-CubeShot-Client-IP"))
            {
                const auto ip = request.get_header_value("X-CubeShot-Client-IP");
                if (!ip.empty() && ip.size() <= 45)
                    task->ip = ip;
            }
            auto future = task->reply.get_future();
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (tasks.size() >= 64)
                {
                    response.status = 503;
                    response.set_content("{\"error\":\"busy\"}", "application/json");
                    return;
                }
                tasks.push_back(task);
            }
            Reply result = error(503, "busy");
            if (future.wait_for(std::chrono::seconds(2)) == std::future_status::ready)
                result = future.get();
            response.status = result.status;
            response.set_header("Cache-Control", "no-store");
            if (result.status != 204)
                response.set_content(result.body.dump(), "application/json; charset=utf-8");
        };
        http.Get("/v1/servers", handler);
        http.Get("/v1/health", handler);
        http.Post("/v1/reservations", handler);
        http.Delete(R"(/v1/reservations/[0-9a-f]{64})", handler);
        int port = config.apiPort;
        if (port)
        {
            if (!http.bind_to_port("127.0.0.1", port))
                throw std::runtime_error("Cannot bind API port");
        }
        else
            port = http.bind_to_any_port("127.0.0.1");
        if (port <= 0)
            throw std::runtime_error("Cannot bind API port");
        boundPort = static_cast<std::uint16_t>(port);
        httpThread = std::thread([this] { http.listen_after_bind(); });
    }

    ~Impl()
    {
        http.stop();
        if (httpThread.joinable())
            httpThread.join();
    }

    Json leaseJson(const Reservations::Lease& lease) const
    {
        const auto& room = pool.rooms()[lease.room];
        return {{"server_id", room.id},
                {"address", config.publicIp + ":" + std::to_string(room.server->port())},
                {"token", lease.token},
                {"expires_in_ms",
                 std::max<std::int64_t>(
                     0, std::chrono::duration_cast<std::chrono::milliseconds>(lease.expires - Clock::now())
                            .count())},
                {"protocol", network::protocolVersion}};
    }

    Reply handle(const Task& task)
    {
        reservations.expire();
        const auto now = Clock::now();
        for (auto it = limits.begin(); it != limits.end();)
        {
            if (now - it->second.first > std::chrono::seconds(2))
                it = limits.erase(it);
            else
                ++it;
        }
        const auto& rooms = pool.rooms();
        auto rate = limits.find(task.ip);
        if (rate == limits.end())
        {
            if (limits.size() >= 4096)
                return error(429, "rate_limited");
            rate = limits.emplace(task.ip, std::make_pair(Clock::now(), 0u)).first;
        }
        if (Clock::now() - rate->second.first >= std::chrono::seconds(1))
            rate->second = {Clock::now(), 0};
        if (++rate->second.second > 20)
            return error(429, "rate_limited");
        if (task.path == "/v1/health")
            return {
                200,
                {{"status", "ok"}, {"protocol", network::protocolVersion}, {"version", CUBESHOT_VERSION}}};
        if (task.method == "GET")
        {
            Json list = Json::array();
            for (std::size_t i = 0; i < rooms.size(); ++i)
            {
                const auto snapshot = rooms[i].server->match().snapshot();
                const auto slots = reservations.occupied(i);
                list.push_back(
                    {{"id", rooms[i].id},
                     {"name", "CubeShot " + std::to_string(i + 1)},
                     {"players", int(snapshot.connected[0]) + int(snapshot.connected[1])},
                     {"reserved", int(slots[0]) + int(slots[1]) - int(snapshot.connected[0]) -
                                      int(snapshot.connected[1])},
                     {"phase", static_cast<unsigned>(snapshot.phase)},
                     {"joinable", snapshot.phase == MatchPhase::Lobby && (!slots[0] || !slots[1])}});
            }
            return {
                200,
                {{"protocol", network::protocolVersion}, {"version", CUBESHOT_VERSION}, {"servers", list}}};
        }
        if (task.method == "DELETE")
        {
            const auto token = task.path.substr(std::string("/v1/reservations/").size());
            reservations.cancel(token);
            return {204, Json::object()};
        }
        const auto body = Json::parse(task.body, nullptr, false);
        if (!body.is_object() || !body.contains("protocol") || !body["protocol"].is_number_unsigned() ||
            !body.contains("nickname") || !body["nickname"].is_string() || !body.contains("request_id") ||
            !body["request_id"].is_string() || (body.contains("server_id") && !body["server_id"].is_string()))
            return error(400, "invalid_request");
        if (body["protocol"] != network::protocolVersion)
            return error(409, "incompatible_protocol");
        const auto name = body["nickname"].get<std::string>();
        const auto request = body["request_id"].get<std::string>();
        const auto server = body.value("server_id", std::string{});
        if (!network::validNickname(name) || !hexToken(request))
            return error(400, "invalid_nickname_or_request");
        const auto result = reservations.reserve(task.ip, name, request, server);
        switch (result.error)
        {
            case Reservations::Error::Conflict:
                return error(409, "request_conflict");
            case Reservations::Error::Cancelled:
                return error(410, "cancelled");
            case Reservations::Error::Full:
                return error(409, "no_free_servers");
            case Reservations::Error::Limit:
                return error(429, "rate_limited");
            case Reservations::Error::None:
                return {result.created ? 201 : 200, leaseJson(*result.lease)};
        }
        return error(500, "internal_error");
    }
};

HttpApi::HttpApi(ServiceConfig config, const ArenaPool& pool, Reservations& reservations)
    : impl_(std::make_unique<Impl>(std::move(config), pool, reservations))
{
}

HttpApi::~HttpApi() = default;

void HttpApi::poll()
{
    std::deque<std::shared_ptr<Impl::Task>> tasks;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        tasks.swap(impl_->tasks);
    }
    for (auto& task : tasks)
    {
        try
        {
            task->reply.set_value(impl_->handle(*task));
        }
        catch (const std::exception&)
        {
            task->reply.set_value(error(400, "invalid_request"));
        }
    }
}

std::uint16_t HttpApi::port() const
{
    return impl_->boundPort;
}
} // namespace cubeshot::online
