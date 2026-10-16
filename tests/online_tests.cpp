#include "online/Service.h"
#include "network/Directory.h"
#include "network/Identity.h"
#include "network/Session.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cubeshot;
using namespace cubeshot::network;
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
void expect(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
struct Reply { int status; Json body; };
struct Rig {
    online::Service service;
    Client host,guest;
    PlayerInput hostInput{},guestInput{};
    Clock::time_point previous=Clock::now(); double accumulator=0;
    static online::ServiceConfig config(std::chrono::milliseconds lease) {
        online::ServiceConfig c; c.apiPort=0; c.firstGamePort=29680; c.leaseDuration=lease; return c;
    }
    explicit Rig(std::chrono::milliseconds lease=std::chrono::seconds(10)):service(config(lease)) {}
    std::string url() const { return "http://127.0.0.1:"+std::to_string(service.apiPort()); }
    void tick() {
        service.poll(); host.poll(); guest.poll(); host.control(hostInput); guest.control(guestInput);
        const auto now=Clock::now(); accumulator+=std::chrono::duration<double>(now-previous).count(); previous=now;
        while (accumulator>=1.0/120) { service.step(); accumulator-=1.0/120; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    template<class F> void until(F condition,double seconds,const char* message) {
        const auto end=Clock::now()+std::chrono::duration<double>(seconds);
        while (!condition() && Clock::now()<end) tick(); expect(condition(),message);
    }
    void run(double seconds) { const auto end=Clock::now()+std::chrono::duration<double>(seconds); while (Clock::now()<end) tick(); }
    std::future<Reply> asyncApi(const std::string& method,const std::string& path,const Json& body=Json::object()) {
        const auto target=url(); return std::async(std::launch::async,[=] {
            httplib::Client client(target); client.set_read_timeout(3); client.set_connection_timeout(2);
            httplib::Result result;
            if (method=="POST") result=client.Post(path,body.dump(),"application/json");
            else if (method=="DELETE") result=client.Delete(path);
            else result=client.Get(path);
            if (!result) throw std::runtime_error("HTTP request failed");
            return Reply{result->status,result->status==204?Json::object():Json::parse(result->body)};
        });
    }
    Reply finish(std::future<Reply>& result) { until([&]{ return result.wait_for(std::chrono::seconds(0))==std::future_status::ready; },4,"API responds without blocking simulation"); return result.get(); }
    Reply api(const std::string& method,const std::string& path,const Json& body=Json::object()) { auto result=asyncApi(method,path,body); return finish(result); }
    Json body(const std::string& name="Игрок",const std::string& server="") { return {{"protocol",protocolVersion},{"nickname",name},{"request_id",randomToken()},{"server_id",server}}; }
};
void identity() {
    expect(validNickname("Ёжик Player_1-2") && validNickname("АБВГДЕЖЗИЙКЛМНОП"),"Latin and Cyrillic nicknames including 16 letters");
    for (const std::string name:{"","    ","ABCDEFGHIJKLMNOPQ","hello\nworld","<script>","😎"}) expect(!validNickname(name),"Reject empty, oversized or unsupported nicknames");
    expect(nicknameText("Игрок").size()==5,"UTF-8 decoded by characters");
    expect(validDirectoryUrl("https://play.example.org") && validDirectoryUrl("http://127.0.0.1:8080"),"HTTPS and loopback development API allowed");
    expect(!validDirectoryUrl("http://example.org") && !validDirectoryUrl("file:///tmp/config") && !validDirectoryUrl("https://x@evil.org"),"No insecure public API or arbitrary URL schemes");
}
void reservationsAndCombat() {
    Rig r;
    auto list=r.api("GET","/v1/servers"); expect(list.status==200 && list.body["servers"].size()==2,"Pool exposes two independent arenas");
    auto invalid=r.body(); invalid["protocol"]=1;
    expect(r.api("POST","/v1/reservations",invalid).status==409,"Old protocol cannot reserve a slot");
    invalid=r.body("😎"); expect(r.api("POST","/v1/reservations",invalid).status==400,"Backend validates nicknames");
    const auto request=r.body("Хозяин"); auto first=r.api("POST","/v1/reservations",request);
    expect(first.status==201,"Reserve first place");
    auto again=r.api("POST","/v1/reservations",request);
    expect(again.status==200 && again.body["token"]==first.body["token"],"Retry returns same token without allocating a second slot");
    auto second=r.api("POST","/v1/reservations",r.body("Гость"));
    expect(second.status==201 && first.body["server_id"]==second.body["server_id"],"Quick match fills a waiting duel before an empty one");
    auto thirdFuture=r.asyncApi("POST","/v1/reservations",r.body("Третий"));
    auto fourthFuture=r.asyncApi("POST","/v1/reservations",r.body("Четвёртый"));
    auto third=r.finish(thirdFuture),fourth=r.finish(fourthFuture);
    expect(third.status==201 && fourth.status==201 && third.body["token"]!=fourth.body["token"],"Concurrent requests allocate distinct slots");
    expect(r.api("POST","/v1/reservations",r.body()).status==409,"Full pool reports no places");
    const auto firstAddress=first.body["address"].get<std::string>();
    Client intruder; intruder.connect(firstAddress);
    r.until([&]{ intruder.poll(); return !intruder.connecting(); },3,"Unreserved connection resolves");
    expect(!intruder.connected(),"Internet servers reject direct unreserved admission");
    r.host.connect(firstAddress,"","Хозяин",first.body["token"]);
    r.guest.connect(second.body["address"],"","Гость",second.body["token"]);
    r.until([&]{ return r.host.connected() && r.guest.connected() && r.host.snapshot() && r.guest.snapshot(); },3,"Both reserved players join");
    expect(r.host.snapshot()->names[0]=="Хозяин" && r.guest.snapshot()->names[1]=="Гость","Trusted names reach both clients");
    Client replay; replay.connect(firstAddress,"","Хозяин",first.body["token"]);
    r.until([&]{ replay.poll(); return !replay.connecting(); },3,"Used token resolves"); expect(!replay.connected(),"Reservation tokens are single use");
    r.host.ready(true); r.guest.ready(true);
    r.until([&]{ return r.host.snapshot()->phase==MatchPhase::Round && r.guest.snapshot()->phase==MatchPhase::Round; },4,"Ready begins online round");
    r.hostInput.moveDirection=1; r.guestInput.moveDirection=-1; r.run(1); r.hostInput={}; r.guestInput={}; r.run(.2);
    for (int i=0;i<5;++i) {
        const auto& players=r.host.snapshot()->players; const auto from=players[0].weaponOrigin(),to=players[1].weaponOrigin();
        r.host.action(false,true,{to.x-from.x,to.y-from.y});
        if (!i) expect(!r.host.interpolated().bullets.empty(),"Own online shot is visible before receiving a server packet");
        r.until([&]{ return r.host.snapshot()->players[1].health==100-(i+1)*20 && r.guest.snapshot()->players[1].health==100-(i+1)*20; },2,"Server damage reaches both clients");
        r.run(.27);
    }
    expect(r.host.snapshot()->score[0]==1 && r.guest.snapshot()->score[0]==1,"Online clients agree on round score");
    r.guest.disconnect(); r.until([&]{return r.host.snapshot()->phase==MatchPhase::Lobby && r.host.snapshot()->score[0]==0;},2,"Disconnect aborts online match without ending the public arena");
    auto replacement=r.api("POST","/v1/reservations",r.body("Новый",first.body["server_id"]));
    expect(replacement.status==201,"Public server accepts a replacement after either player leaves");
    r.api("DELETE","/v1/reservations/"+replacement.body["token"].get<std::string>());
    r.api("DELETE","/v1/reservations/"+third.body["token"].get<std::string>());
    auto freed=r.api("POST","/v1/reservations",r.body("Замена",third.body["server_id"]));
    expect(freed.status==201,"Cancellation releases an unconsumed reservation");
}
void expiryAndAsyncCancellation() {
    Rig r(std::chrono::milliseconds(150));
    auto first=r.api("POST","/v1/reservations",r.body()); r.run(.2);
    r.host.connect(first.body["address"],"","Игрок",first.body["token"]);
    r.until([&]{return !r.host.connecting();},2,"Expired admission resolves"); expect(!r.host.connected(),"Expired lease cannot be used");
    auto list=r.api("GET","/v1/servers"); expect(list.body["servers"][0]["reserved"]==0,"Expiry frees slots");
    Directory directory; directory.reserve(r.url(),"Отмена"); directory.cancel();
    const auto end=Clock::now()+std::chrono::milliseconds(300);
    while (Clock::now()<end) { r.tick(); directory.poll(); }
    list=r.api("GET","/v1/servers"); expect(list.body["servers"][0]["reserved"]==0,"Cancelling in-flight request leaves no live reservation");
    directory.list(r.url()); std::optional<DirectoryResult> result;
    r.until([&]{if (!result) result=directory.poll();return bool(result);},3,"Async directory response available");
    expect(result->error.empty() && result->rooms.size()==2,"Client parses backend list");
}
void restartInvalidatesReservations() {
    std::string token,address;
    {
        Rig r; const auto lease=r.api("POST","/v1/reservations",r.body());
        token=lease.body["token"]; address=lease.body["address"];
    }
    Rig restarted;
    restarted.host.connect(address,"","Игрок",token);
    restarted.until([&]{return !restarted.host.connecting();},2,"Old token resolves after service restart");
    expect(!restarted.host.connected(),"Restart invalidates prior reservations");
    const auto health=restarted.api("GET","/v1/health");
    expect(health.status==200 && health.body["status"]=="ok","Restarted pool is healthy");
    auto conflict=Rig::config(std::chrono::seconds(10)); conflict.firstGamePort=29690; conflict.apiPort=restarted.service.apiPort();
    bool rejected=false;
    try { online::Service duplicate(conflict); } catch (const std::exception&) { rejected=true; }
    expect(rejected,"API port is owned exclusively rather than silently shared");
}
int main() {
    try { identity(); reservationsAndCombat(); expiryAndAsyncCancellation(); restartInvalidatesReservations(); std::cout<<"Online pool, reservations, lifecycle and combat passed\n"; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
