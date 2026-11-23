#pragma once
#include "game/Player.h"
#include <SDL3/SDL.h>
#include <string>

namespace cubeshot::client
{
bool drawActiveIcon(SDL_Renderer* renderer, ActiveKind kind, Rect box);
bool drawActiveHud(SDL_Renderer* renderer, const Player& player, const std::array<std::u32string, 2>& keys);
bool drawAbilityIcon(SDL_Renderer* renderer, AbilityKind kind, Rect box);
bool drawAbilityRow(SDL_Renderer* renderer, const Abilities& abilities, Vec2 at, float size = 24,
                    const Player* owner = nullptr);
bool drawAbilityHud(SDL_Renderer* renderer, const Player& player);
} // namespace cubeshot::client
