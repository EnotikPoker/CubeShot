#pragma once
#include "game/Types.h"

struct SDL_Renderer;

namespace cubeshot
{
class World;
struct WorldSnapshot;
} // namespace cubeshot

namespace cubeshot::client
{

class Effects;
bool drawCombatFeedback(SDL_Renderer* renderer, const Effects& effects, Vec2 cursor);
bool renderSnapshot(SDL_Renderer* renderer, const WorldSnapshot& snapshot, PlayerId own,
                    const Effects* effects = nullptr);

bool renderScene(SDL_Renderer* renderer, const World& world, bool present = true,
                 const Effects* effects = nullptr);

} // namespace cubeshot::client
