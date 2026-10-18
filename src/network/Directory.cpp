#include "network/Directory.h"
#include "network/Protocol.h"
#include "network/Session.h"
#include "network/Identity.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <future>
#include <regex>
namespace cubeshot::network {
namespace {
using Json=nlohmann::json;
DirectoryResult request(const std::string& url,DirectoryResult::Kind kind,const std::string& name,
                        const std::string& server,const std::string& token,const std::string& requestId) {
    DirectoryResult result; result.kind=kind;
    if (!validDirectoryUrl(url)) { result.error="not_configured"; return result; }
    try {
        httplib::Client http(url); http.set_connection_timeout(2); http.set_read_timeout(2); http.set_write_timeout(2);
        http.set_max_timeout(3000); http.set_payload_max_length(64*1024);
        httplib::Result reply;
        if (kind==DirectoryResult::Kind::Rooms) reply=http.Get("/v1/servers");
        else if (kind==DirectoryResult::Kind::Released) reply=http.Delete("/v1/reservations/"+token);
        else {
            Json body{{"protocol",protocolVersion},{"nickname",name},{"request_id",requestId},{"server_id",server}};
            reply=http.Post("/v1/reservations",body.dump(),"application/json");
        }
        if (!reply) { result.error="unavailable"; return result; }
        if (kind==DirectoryResult::Kind::Released && reply->status==204) return result;
        const auto body=Json::parse(reply->body,nullptr,false);
        if (!body.is_object()) { result.error="invalid_response"; return result; }
        if (reply->status<200 || reply->status>=300) { result.error=body.value("error",std::string("unavailable")); return result; }
        if (!body.contains("protocol") || body["protocol"]!=protocolVersion) { result.error="incompatible_protocol"; return result; }
        if (kind==DirectoryResult::Kind::Reserved) {
            result.address=body.at("address").get<std::string>(); result.token=body.at("token").get<std::string>();
            if (!validAddress(result.address) || result.token.size()!=64 || result.token.find_first_not_of("0123456789abcdef")!=std::string::npos)
                throw std::runtime_error("Invalid reservation");
        } else {
            const auto& rooms=body.at("servers"); if (!rooms.is_array() || rooms.size()>32) throw std::runtime_error("Invalid server list");
            for (const auto& room:rooms) {
                DirectoryRoom entry{room.at("id").get<std::string>(),room.at("name").get<std::string>(),
                    room.at("players").get<unsigned>(),room.at("reserved").get<unsigned>(),room.at("phase").get<unsigned>(),room.at("joinable").get<bool>()};
                if (entry.id.empty() || entry.id.size()>64 || entry.name.size()>32 || entry.players>2 || entry.reserved>2 || entry.players+entry.reserved>2 || entry.phase>4)
                    throw std::runtime_error("Invalid server entry");
                result.rooms.push_back(std::move(entry));
            }
        }
    } catch (const std::exception&) { result.error="invalid_response"; }
    return result;
}
}
bool validDirectoryUrl(const std::string& url) {
    if (url.size()>255) return false;
    static const std::regex https(R"(^https://[A-Za-z0-9.-]+(:[0-9]{1,5})?$)");
    static const std::regex local(R"(^http://127\.0\.0\.1:[0-9]{1,5}$)");
    return std::regex_match(url,https) || std::regex_match(url,local);
}
struct Directory::Impl {
    struct Job {
        std::string url;
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::future<DirectoryResult> future;
        bool background=false;
    };
    std::vector<Job> jobs;
    std::optional<DirectoryResult> immediate;
    void start(const std::string& url,DirectoryResult::Kind kind,const std::string& name={},const std::string& server={},const std::string& token={},bool background=false) {
        if (jobs.size()>=16) { DirectoryResult result; result.kind=kind; result.error="rate_limited"; immediate=std::move(result); return; }
        auto cancelled=std::make_shared<std::atomic<bool>>(false);
        const auto id=kind==DirectoryResult::Kind::Reserved ? randomToken():std::string{};
        auto future=std::async(std::launch::async,[=] {
            auto result=request(url,kind,name,server,token,id);
            if (*cancelled && !result.token.empty()) { request(url,DirectoryResult::Kind::Released,"","",result.token,""); result.token.clear(); }
            return result;
        });
        jobs.push_back({url,cancelled,std::move(future),background});
    }
};
Directory::Directory():impl_(std::make_unique<Impl>()) {}
Directory::~Directory() {
    cancel();
    for (auto& job:impl_->jobs) {
        auto result=job.future.get();
        if (!result.token.empty()) request(job.url,DirectoryResult::Kind::Released,"","",result.token,"");
    }
}
void Directory::cancel() { impl_->immediate.reset(); for (auto& job:impl_->jobs) if (!job.background) *job.cancelled=true; }
void Directory::list(const std::string& url) { cancel(); impl_->start(url,DirectoryResult::Kind::Rooms); }
void Directory::reserve(const std::string& url,const std::string& name,const std::string& server) { cancel(); impl_->start(url,DirectoryResult::Kind::Reserved,name,server); }
void Directory::release(const std::string& url,const std::string& token) { if (!token.empty()) impl_->start(url,DirectoryResult::Kind::Released,"","",token,true); }
bool Directory::busy() const {
    for (const auto& job:impl_->jobs) if (!job.background && !*job.cancelled) return true;
    return false;
}
std::optional<DirectoryResult> Directory::poll() {
    std::optional<DirectoryResult> ready;
    std::vector<std::pair<std::string,std::string>> cleanup;
    for (auto it=impl_->jobs.begin();it!=impl_->jobs.end();) {
        if (it->future.wait_for(std::chrono::seconds(0))!=std::future_status::ready) { ++it; continue; }
        auto result=it->future.get();
        if (*it->cancelled && !result.token.empty()) cleanup.push_back({it->url,result.token});
        else if (!it->background && !*it->cancelled) ready=std::move(result);
        it=impl_->jobs.erase(it);
    }
    for (const auto& entry:cleanup) release(entry.first,entry.second);
    if (impl_->immediate) { ready=std::move(impl_->immediate); impl_->immediate.reset(); }
    return ready;
}
}
