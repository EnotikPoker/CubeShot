#include "client/HostProcess.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <random>
#include <sstream>
namespace cubeshot::client {
HostProcess::~HostProcess() { stop(); }
bool HostProcess::start() {
    stop(); error_.clear();
    const char* base = SDL_GetBasePath();
    if (!base) { error_ = SDL_GetError(); return false; }
#ifdef _WIN32
    const auto executable = std::filesystem::u8path(base) / "cubeshot_server.exe";
#else
    const auto executable = std::filesystem::u8path(base) / "cubeshot_server";
#endif
    if (!std::filesystem::exists(executable)) { error_ = "cubeshot_server is missing next to client"; return false; }
    std::ostringstream token;
    for (int i = 0; i < 4; ++i) token << std::hex << std::random_device{}();
    token_ = token.str();
    const std::string path = executable.u8string();
    const char* args[] = {path.c_str(), "--owner-token", token_.c_str(), nullptr};
    process_ = SDL_CreateProcess(args, false);
    if (!process_) { error_ = SDL_GetError(); return false; }
    return true;
}
bool HostProcess::running() {
    if (!process_) return false;
    int status = 0;
    if (!SDL_WaitProcess(process_, false, &status)) return true;
    error_ = "Server exited (game port may be occupied)";
    SDL_DestroyProcess(process_); process_ = nullptr;
    return false;
}
void HostProcess::stop() {
    if (!process_) return;
    int status = 0;
    if (!SDL_WaitProcess(process_, false, &status)) {
        SDL_KillProcess(process_, false);
        const auto deadline = SDL_GetTicks() + 1000;
        while (!SDL_WaitProcess(process_, false, &status) && SDL_GetTicks() < deadline) SDL_Delay(5);
        if (!SDL_WaitProcess(process_, false, &status)) {
            SDL_KillProcess(process_, true);
            SDL_WaitProcess(process_, true, &status);
        }
    }
    SDL_DestroyProcess(process_); process_ = nullptr;
}
}
