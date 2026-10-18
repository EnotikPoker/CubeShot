#include "client/Profile.h"
#include "network/Directory.h"
#include "network/Identity.h"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
namespace cubeshot::client {
namespace {
std::filesystem::path path() {
    char* base=SDL_GetPrefPath("CubeShot","CubeShot");
    if (!base) return {};
    auto result=std::filesystem::u8path(base)/"profile.json"; SDL_free(base); return result;
}
}
Profile loadProfile() {
    Profile profile;
    try {
        const auto file=path(); if (file.empty() || !std::filesystem::exists(file) || std::filesystem::file_size(file)>4096) return profile;
        std::ifstream input(file); const auto data=nlohmann::json::parse(input,nullptr,false);
        if (!data.is_object()) return profile;
        const auto name=data.value("nickname",std::string{}),url=data.value("directory_url",std::string{});
        if (network::validNickname(name)) profile.nickname=name;
        if (network::validDirectoryUrl(url)) profile.directoryUrl=url;
    } catch (const std::exception&) { /* Missing or damaged preference falls back to defaults. */ }
    return profile;
}
void saveProfile(const Profile& profile) {
    try {
        const auto file=path(); if (file.empty()) return;
        std::ofstream output(file); output << nlohmann::json{{"nickname",profile.nickname},{"directory_url",profile.directoryUrl}}.dump(2);
    } catch (const std::exception&) { SDL_Log("Cannot save player profile"); }
}
}
