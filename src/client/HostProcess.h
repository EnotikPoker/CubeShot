#pragma once
#include <string>
struct SDL_Process;
namespace cubeshot::client {
class HostProcess {
public:
    ~HostProcess();
    bool start();
    void stop();
    bool running();
    const std::string& token() const { return token_; }
    const std::string& error() const { return error_; }
private:
    SDL_Process* process_ = nullptr;
    std::string token_, error_;
};
}
