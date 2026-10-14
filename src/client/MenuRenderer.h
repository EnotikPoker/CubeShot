#pragma once
#include "game/Match.h"

struct SDL_Renderer;

namespace cubeshot::client {
class Menu;
bool renderMatchHud(SDL_Renderer* renderer, const WorldSnapshot& snapshot, PlayerId own);

bool renderMenu(SDL_Renderer* renderer, const Menu& menu);
} // namespace cubeshot::client
