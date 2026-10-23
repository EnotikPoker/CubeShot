#include "client/SceneRenderer.h"
#include "client/Effects.h"
#include "client/WorldView.h"
#include "game/Match.h"
#include "game/World.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

namespace cubeshot::client
{
namespace
{
struct Painter
{
    SDL_Renderer* renderer;
    WorldView view;

    bool rect(Rect r, SDL_Color color) const
    {
        auto at = view.toScreen(r.position);
        SDL_FRect box{at.x, at.y, r.size.x * view.scale, r.size.y * view.scale};
        return SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a) &&
               SDL_RenderFillRect(renderer, &box);
    }

    bool part(Vec2 origin, Vec2 direction, float x, float y, float w, float h, SDL_FColor color,
              bool mirror = false) const
    {
        auto vertex = [&](float along, float across)
        {
            if (mirror)
                across = -across;
            auto at = view.toScreen({origin.x + direction.x * along - direction.y * across,
                                     origin.y + direction.y * along + direction.x * across});
            return SDL_Vertex{{at.x, at.y}, color, {0, 0}};
        };
        SDL_Vertex vertices[]{vertex(x, y), vertex(x + w, y), vertex(x + w, y + h), vertex(x, y + h)};
        const int indices[]{0, 1, 2, 0, 2, 3};
        return SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
    }
};

bool weapon(const Painter& p, const Player& player, const Pose& pose)
{
    Vec2 origin = player.weaponOrigin(), aim = player.aimDirection;
    const float recoil = pose.shot / .1f;
    origin.x -= aim.x * recoil * 6;
    origin.y -= aim.y * recoil * 6;
    const bool mirror = aim.x < 0;
    if (!p.part(origin, aim, 0, -3, 15, 8, {.94f, .73f, .49f, 1}, mirror) ||
        !p.part(origin, aim, 12, 0, 8, 15, {.22f, .25f, .3f, 1}, mirror) ||
        !p.part(origin, aim, 10, -5, Weapon::barrelLength - 10, 10, {.7f, .75f, .82f, 1}, mirror) ||
        !p.part(origin, aim, 11, -5, Weapon::barrelLength - 12, 3, {.91f, .94f, 1, 1}, mirror))
        return false;
    return pose.shot < .055f ||
           p.part(player.muzzlePosition(), aim, 0, -6, 12 + recoil * 7, 12, {1, .86f, .3f, 1});
}

bool background(const Painter& p, const Arena& arena)
{
    const auto theme = mapInfo(arena.map).theme;
    // Muted decoration never resembles the bright, solid collision surfaces.
    if (theme == MapTheme::Factory)
    {
        for (float x = 35; x < arena.bounds.size.x; x += 160)
        {
            if (!p.rect({{x, 80}, {12, arena.bounds.size.y - 80}}, {30, 40, 52, 255}) ||
                !p.rect({{x, 80}, {100, 10}}, {36, 46, 58, 255}))
                return false;
        }
    }
    else if (theme == MapTheme::Ruins)
    {
        for (float x = 40; x < arena.bounds.size.x; x += 180)
            if (!p.rect({{x, 140}, {50, arena.bounds.size.y - 140}}, {38, 36, 49, 255}) ||
                !p.rect({{x - 10, 130}, {70, 20}}, {45, 42, 54, 255}))
                return false;
    }
    else if (theme == MapTheme::Nature)
    {
        for (float x = 40; x < arena.bounds.size.x; x += 170)
            if (!p.rect({{x, 160}, {18, arena.bounds.size.y - 160}}, {26, 47, 40, 255}) ||
                !p.rect({{x - 42, 90}, {102, 110}}, {27, 55, 45, 255}))
                return false;
    }
    return true;
}
} // namespace

static bool renderWorld(SDL_Renderer* renderer, const Arena& arena, const std::vector<Player>& players,
                        const std::vector<Bullet>& bullets, PlayerId own, bool present,
                        const Effects* effects)
{
    const auto theme = mapInfo(arena.map).theme;
    const SDL_Color bg = theme == MapTheme::Nature  ? SDL_Color{19, 32, 31, 255}
                         : theme == MapTheme::Ruins ? SDL_Color{28, 26, 36, 255}
                                                    : SDL_Color{24, 28, 38, 255};
    if (!SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a) || !SDL_RenderClear(renderer))
        return false;
    Painter p{renderer, WorldView(arena.bounds)};
    if (effects)
    {
        const auto shake = effects->shakeOffset();
        p.view.offset.x += shake.x;
        p.view.offset.y += shake.y;
    }
    if (!background(p, arena))
        return false;
    for (const auto& r : arena.platforms)
    {
        SDL_Color color = theme == MapTheme::Factory  ? SDL_Color{90, 110, 130, 255}
                          : theme == MapTheme::Ruins  ? SDL_Color{121, 112, 126, 255}
                          : theme == MapTheme::Nature ? SDL_Color{98, 112, 80, 255}
                                                      : SDL_Color{95, 110, 125, 255};
        if (!p.rect(r, color))
            return false;
        if (theme != MapTheme::Classic)
        {
            if (!p.rect({r.position, {r.size.x, 4}}, theme == MapTheme::Nature
                                                         ? SDL_Color{114, 167, 95, 255}
                                                         : SDL_Color{177, 166, 125, 255}))
                return false;
            for (float x = r.position.x + 12; x < r.position.x + r.size.x - 8; x += 48)
                if (!p.rect({{x, r.position.y + 8}, {6, 3}}, theme == MapTheme::Factory
                                                                 ? SDL_Color{46, 58, 70, 255}
                                                                 : SDL_Color{71, 73, 67, 255}))
                    return false;
        }
    }
    if (effects)
        for (const auto& stain : effects->stains())
            if (!p.rect({{stain.position.x - stain.width / 2, stain.position.y - stain.height / 2},
                         {stain.width, stain.height}},
                        {119, 27, 43, 255}))
                return false;
    for (const auto& player : players)
    {
        const Pose pose = effects ? effects->pose(player.id) : Pose{};
        const bool local = player.id == own;
        SDL_Color color = pose.hit > 0 ? SDL_Color{255, 230, 225, 255}
                          : local      ? SDL_Color{80, 175, 255, 255}
                                       : SDL_Color{230, 85, 85, 255};
        if (!player.alive())
        {
            if (!effects)
                continue;
            const float t = std::min(1.f, pose.death / .22f);
            const auto pos = pose.corpse;
            const float angle = std::sin(t * 3.14159265f) * .65f;
            const Vec2 direction{std::cos(angle), std::sin(angle)};
            const Vec2 pivot{pos.x + 24, pos.y + 48 - std::abs(direction.y) * 24};
            if (!p.part(pivot, direction, -24, -48 + 30 * t, 48, 48 - 30 * t,
                        {color.r / 510.f, color.g / 510.f, color.b / 510.f, 1}))
                return false;
            continue;
        }
        float squash = pose.land > 0 ? std::sin(pose.land / .16f * 3.14159265f) * .14f : 0;
        float walk = player.grounded ? std::sin(pose.cycle) * .035f : 0;
        float stretch = !player.grounded ? std::clamp(-player.velocity.y / 620.f, -.08f, .08f) : 0;
        Rect body = player.bounds();
        body.size.x *= 1 + squash + walk - stretch;
        body.size.y *= 1 - squash - walk + stretch;
        body.position.x += (player.size.x - body.size.x) / 2;
        body.position.y += player.size.y - body.size.y;
        if (!p.rect(body, color) || !weapon(p, player, pose))
            return false;
        // A small gaze mark gives the otherwise square body a clear facing direction.
        if (!p.rect({{body.position.x + (player.aimDirection.x < 0 ? 8.f : body.size.x - 12),
                      body.position.y + 12},
                     {4, 4}},
                    {24, 36, 52, 255}))
            return false;
        const Vec2 hp{player.position.x, std::max(player.position.y - 12, arena.bounds.position.y + 1)};
        const float f =
            static_cast<float>(std::clamp(player.health, 0, Player::maxHealth)) / Player::maxHealth;
        if (!p.rect({{hp.x - 1, hp.y - 1}, {player.size.x + 2, 8}}, {12, 16, 24, 255}) ||
            !p.rect({hp, {player.size.x, 6}}, {80, 35, 45, 255}) ||
            !p.rect({hp, {player.size.x * f, 6}}, {85, 210, 125, 255}))
            return false;
    }
    for (const auto& bullet : bullets)
    {
        const float speed = std::hypot(bullet.velocity.x, bullet.velocity.y);
        const Vec2 dir = speed > 0 ? Vec2{bullet.velocity.x / speed, bullet.velocity.y / speed} : Vec2{1, 0};
        if (!p.part(bullet.position, dir, -15, -1.5f, 15, 3, {1, .55f, .12f, 1}) ||
            !p.rect({{bullet.position.x - Bullet::radius, bullet.position.y - Bullet::radius},
                     {Bullet::radius * 2, Bullet::radius * 2}},
                    {255, 240, 150, 255}))
            return false;
    }
    if (effects)
        for (const auto& particle : effects->particles())
        {
            if (!p.rect({particle.position, {particle.blood ? 3.f : 2.f, particle.blood ? 3.f : 2.f}},
                        particle.blood ? SDL_Color{204, 35, 59, 255} : SDL_Color{250, 196, 95, 255}))
                return false;
        }
    return !present || SDL_RenderPresent(renderer);
}

bool renderScene(SDL_Renderer* renderer, const World& world, bool present, const Effects* effects)
{
    return renderWorld(renderer, world.arena(), world.players(), world.bullets(), World::localPlayerId,
                       present, effects);
}

bool renderSnapshot(SDL_Renderer* renderer, const WorldSnapshot& snapshot, PlayerId own,
                    const Effects* effects)
{
    return renderWorld(renderer, Arena::create(snapshot.map), snapshot.players, snapshot.bullets, own, false,
                       effects);
}
} // namespace cubeshot::client
