#pragma once
#include <filesystem>
#include <string>

namespace cubeshot::client
{
struct Profile
{
    std::string nickname = "Игрок", directoryUrl;
};

Profile loadProfile();
Profile loadProfile(const std::filesystem::path& file);
bool saveProfile(const Profile& profile, std::string* error = nullptr);
bool saveProfile(const Profile& profile, const std::filesystem::path& file, std::string& error);
} // namespace cubeshot::client
