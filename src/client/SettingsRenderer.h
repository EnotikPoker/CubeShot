#pragma once
#include <cstdint>
struct SDL_Renderer;

namespace cubeshot::client
{
class SettingsController;
bool renderSettings(SDL_Renderer* renderer, const SettingsController& settings, std::uint64_t now,
                    bool networkMatch);
} // namespace cubeshot::client
