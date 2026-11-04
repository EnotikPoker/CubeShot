#pragma once
#include "game/Player.h"
#include <SDL3/SDL.h>

namespace cubeshot::client
{
bool drawAbilityIcon(SDL_Renderer* renderer, AbilityKind kind, Rect box);
bool drawAbilityRow(SDL_Renderer* renderer, const Abilities& abilities, Vec2 at, float size = 24);
bool drawAbilityHud(SDL_Renderer* renderer, const Player& player);
} // namespace cubeshot::client
