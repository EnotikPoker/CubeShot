#pragma once
#include "client/Settings.h"
#include <filesystem>
#include <string>

namespace cubeshot::client
{
std::filesystem::path preferencePath(const char* filename);
bool writePreference(const std::filesystem::path& file, const std::string& contents, std::string& error);
Settings loadSettings(const std::filesystem::path& file);
Settings loadSettings();
bool saveSettings(const Settings& settings, const std::filesystem::path& file, std::string& error);
bool saveSettings(const Settings& settings, std::string& error);
} // namespace cubeshot::client
