#include "client/Profile.h"
#include "client/SettingsStore.h"
#include "network/Directory.h"
#include "network/Identity.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace cubeshot::client
{
Profile loadProfile(const std::filesystem::path& file)
{
    Profile profile;
    try
    {
        if (file.empty() || !std::filesystem::exists(file) || std::filesystem::file_size(file) > 4096)
            return profile;
        std::ifstream input(file);
        const auto data = nlohmann::json::parse(input, nullptr, false);
        if (!data.is_object())
            return profile;
        const auto name = data.value("nickname", std::string{}),
                   url = data.value("directory_url", std::string{});
        if (network::validNickname(name))
            profile.nickname = name;
        if (network::validDirectoryUrl(url))
            profile.directoryUrl = url;
    }
    catch (const std::exception&)
    { /* Missing or damaged preference falls back to defaults. */
    }
    return profile;
}

Profile loadProfile()
{
    return loadProfile(preferencePath("profile.json"));
}

bool saveProfile(const Profile& profile, const std::filesystem::path& file, std::string& error)
{
    return writePreference(
        file, nlohmann::json{{"nickname", profile.nickname}, {"directory_url", profile.directoryUrl}}.dump(2),
        error);
}

bool saveProfile(const Profile& profile, std::string* error)
{
    std::string detail;
    const bool ok = saveProfile(profile, preferencePath("profile.json"), detail);
    if (error)
        *error = detail;
    return ok;
}
} // namespace cubeshot::client
