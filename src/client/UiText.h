#pragma once

#include "game/Types.h"

#include <string_view>

struct SDL_Renderer;
struct SDL_Color;

namespace cubeshot::client
{

float textHeight(SDL_Renderer* renderer, int scale);
float textWidth(SDL_Renderer* renderer, std::u32string_view text, int scale);
bool drawText(SDL_Renderer* renderer, std::u32string_view text, Vec2 position, int scale, SDL_Color color);

} // namespace cubeshot::client
