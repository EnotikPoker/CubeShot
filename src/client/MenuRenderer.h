#pragma once

struct SDL_Renderer;

namespace cubeshot::client {
class Menu;
bool renderMenu(SDL_Renderer* renderer, const Menu& menu);
} // namespace cubeshot::client
