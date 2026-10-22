#include "client/SettingsStore.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace cubeshot::client
{
std::filesystem::path preferencePath(const char* filename)
{
    char* base = SDL_GetPrefPath("CubeShot", "CubeShot");
    if (!base)
        return {};
    const auto result = std::filesystem::u8path(base) / filename;
    SDL_free(base);
    return result;
}

bool writePreference(const std::filesystem::path& file, const std::string& contents, std::string& error)
{
    std::filesystem::path temporary;
    try
    {
        if (file.empty())
            throw std::runtime_error("Preference directory is unavailable");
        temporary = file;
        temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.exceptions(std::ios::failbit | std::ios::badbit);
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.flush();
            output.close();
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace preference file");
#else
        std::filesystem::rename(temporary, file);
#endif
        error.clear();
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        std::error_code ignored;
        if (!temporary.empty())
            std::filesystem::remove(temporary, ignored);
        return false;
    }
}

Settings loadSettings(const std::filesystem::path& file)
{
    Settings settings;
    try
    {
        if (file.empty() || !std::filesystem::exists(file) || std::filesystem::file_size(file) > 16384)
            return settings;
        std::ifstream input(file);
        const auto data = nlohmann::json::parse(input, nullptr, false);
        if (!data.is_object() || data.value("version", 0) != 1)
            return settings;
        try
        {
            const auto& d = data.at("display");
            if (!d.at("width").is_number_integer() || !d.at("height").is_number_integer() ||
                !d.at("fps").is_number_integer())
                throw std::runtime_error("Non-integer display parameter");
            DisplaySettings display{d.at("fullscreen").get<bool>(),
                                    {d.at("width").get<int>(), d.at("height").get<int>()},
                                    d.at("vsync").get<bool>(),
                                    d.at("fps").get<int>()};
            if (validDisplay(display))
                settings.display = display;
        }
        catch (const std::exception&)
        {
        }
        try
        {
            const auto& rows = data.at("bindings");
            if (!rows.is_array() || rows.size() != settings.bindings.size())
                return settings;
            InputBindings bindings{};
            for (std::size_t a = 0; a < bindings.size(); ++a)
            {
                if (!rows[a].is_array() || rows[a].size() != 2)
                    return settings;
                for (unsigned s = 0; s < 2; ++s)
                {
                    if (!rows[a][s].at("device").is_number_integer() ||
                        !rows[a][s].at("code").is_number_integer())
                        return settings;
                    const auto device = rows[a][s].at("device").get<int>();
                    if (device < 0 || device > 2)
                        return settings;
                    bindings[a][s] = {static_cast<BindingDevice>(device), rows[a][s].at("code").get<int>()};
                }
            }
            if (validBindings(bindings))
                settings.bindings = bindings;
        }
        catch (const std::exception&)
        {
        }
    }
    catch (const std::exception&)
    {
    }
    return settings;
}

Settings loadSettings()
{
    return loadSettings(preferencePath("settings.json"));
}

bool saveSettings(const Settings& settings, const std::filesystem::path& file, std::string& error)
{
    if (!validDisplay(settings.display) || !validBindings(settings.bindings))
    {
        error = "Invalid settings";
        return false;
    }
    nlohmann::json rows = nlohmann::json::array();
    for (const auto& row : settings.bindings)
    {
        nlohmann::json slots = nlohmann::json::array();
        for (const auto& binding : row)
            slots.push_back({{"device", static_cast<int>(binding.device)}, {"code", binding.code}});
        rows.push_back(std::move(slots));
    }
    const auto& d = settings.display;
    const nlohmann::json data{{"version", 1},
                              {"display",
                               {{"fullscreen", d.fullscreen},
                                {"width", d.size.width},
                                {"height", d.size.height},
                                {"vsync", d.vsync},
                                {"fps", d.fps}}},
                              {"bindings", rows}};
    return writePreference(file, data.dump(2), error);
}

bool saveSettings(const Settings& settings, std::string& error)
{
    return saveSettings(settings, preferencePath("settings.json"), error);
}
} // namespace cubeshot::client
