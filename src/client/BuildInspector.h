#pragma once
#include "game/Player.h"
#include <SDL3/SDL.h>
#include <array>
#include <string>

namespace cubeshot::client
{
bool drawBuildInspector(SDL_Renderer* r, const std::vector<Player>& players,
                        const std::array<std::u32string, 2>& names, const std::array<int, 2>& scroll,
                        Vec2 mouse);
}
