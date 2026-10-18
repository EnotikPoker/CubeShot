#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace cubeshot::network {
struct DirectoryRoom {
    std::string id, name;
    unsigned players=0, reserved=0, phase=0;
    bool joinable=false;
};
struct DirectoryResult {
    enum class Kind { Rooms, Reserved, Released } kind=Kind::Rooms;
    std::vector<DirectoryRoom> rooms;
    std::string address, token, error;
};
bool validDirectoryUrl(const std::string& url);
// UI-thread facade. All HTTP and cancellation cleanup run on workers.
class Directory {
public:
    Directory();
    ~Directory();
    void list(const std::string& url);
    void reserve(const std::string& url,const std::string& nickname,const std::string& serverId={});
    void release(const std::string& url,const std::string& token);
    void cancel();
    std::optional<DirectoryResult> poll();
    bool busy() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
