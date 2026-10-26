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

    bool ellipse(Vec2 at, float rx, float ry, SDL_Color color, float angle = 0) const
    {
        constexpr int sides = 14;
        SDL_Vertex vertices[sides + 1];
        auto vertex = [&](Vec2 point)
        {
            auto screen = view.toScreen(point);
            return SDL_Vertex{{screen.x, screen.y},
                              {color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f},
                              {0, 0}};
        };
        vertices[0] = vertex(at);
        for (int n = 0; n < sides; ++n)
        {
            const float t = static_cast<float>(n) * 6.283185307f / sides;
            const Vec2 local{std::cos(t) * rx, std::sin(t) * ry};
            vertices[n + 1] = vertex({at.x + local.x * std::cos(angle) - local.y * std::sin(angle),
                                      at.y + local.x * std::sin(angle) + local.y * std::cos(angle)});
        }
        int indices[sides * 3];
        for (int n = 0; n < sides; ++n)
        {
            indices[n * 3] = 0;
            indices[n * 3 + 1] = n + 1;
            indices[n * 3 + 2] = (n + 1) % sides + 1;
        }
        return SDL_RenderGeometry(renderer, nullptr, vertices, sides + 1, indices, sides * 3);
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
    const auto kind = player.weapon.kind;
    const float length = player.weapon.barrel();
    const bool longGun = kind == WeaponKind::Shotgun || kind == WeaponKind::Automatic ||
                         kind == WeaponKind::Rifle || kind == WeaponKind::Sniper;
    const SDL_FColor steel = kind == WeaponKind::Smg        ? SDL_FColor{.3f, .34f, .4f, 1}
                             : kind == WeaponKind::Revolver ? SDL_FColor{.8f, .82f, .87f, 1}
                                                            : SDL_FColor{.65f, .7f, .76f, 1};
    if (!p.part(origin, aim, 0, -3, 15, 8, {.94f, .73f, .49f, 1}, mirror) ||
        !p.part(origin, aim, 12, 0, 8, 15, {.2f, .23f, .28f, 1}, mirror) ||
        !p.part(origin, aim, 10, -5, length - 10, 10, steel, mirror) ||
        !p.part(origin, aim, 11, -5, length - 12, 3, {.9f, .93f, 1, 1}, mirror))
        return false;
    if (longGun && !p.part(origin, aim, -7, -3, 19, 12, {.37f, .25f, .17f, 1}, mirror))
        return false;
    if (kind == WeaponKind::Automatic || kind == WeaponKind::Smg)
        if (!p.part(origin, aim, 22, 3, 9, 15, {.25f, .29f, .32f, 1}, mirror))
            return false;
    if (kind == WeaponKind::Revolver && !p.part(origin, aim, 16, -7, 12, 13, {.4f, .43f, .5f, 1}, mirror))
        return false;
    if (kind == WeaponKind::Shotgun && !p.part(origin, aim, 25, 0, 14, 7, {.47f, .29f, .16f, 1}, mirror))
        return false;
    if (kind == WeaponKind::Sniper && (!p.part(origin, aim, 15, -14, 23, 6, {.17f, .2f, .24f, 1}, mirror) ||
                                       !p.part(origin, aim, 21, -9, 5, 5, {.3f, .33f, .37f, 1}, mirror)))
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
                        const Effects* effects, const std::vector<Corpse>& corpses,
                        const std::vector<PhysicsPiece>& pieces)
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
        {
            if (!p.ellipse(stain.position, stain.width / 2, stain.height / 2, {108, 20, 35, 255},
                           stain.angle) ||
                !p.ellipse({stain.position.x + stain.width * .23f, stain.position.y - 1}, stain.width * .31f,
                           stain.height * .4f, {142, 26, 40, 255}, -.4f))
                return false;
        }
    auto remainsColor = [&](PlayerId owner)
    { return owner == own ? SDL_FColor{.22f, .47f, .68f, 1} : SDL_FColor{.65f, .22f, .24f, 1}; };
    const bool blood = !effects || effects->bloodVisible();
    for (const auto& corpse : corpses)
        for (unsigned cell = 0; cell < 16; ++cell)
            if (corpse.cells & (1u << cell))
            {
                auto r = corpseCell(cell);
                const Vec2 direction{std::cos(corpse.body.angle), std::sin(corpse.body.angle)};
                if (!p.part(corpse.body.position, direction, r.position.x, r.position.y, r.size.x - .3f,
                            r.size.y - .3f, remainsColor(corpse.body.owner)))
                    return false;
                if (blood && (cell % 3 == 0 || (corpse.integrity < 50 && cell % 2 == 0)))
                    if (!p.part(corpse.body.position, direction, r.position.x + 2, r.position.y + 1, 6, 5,
                                {.42f, .05f, .1f, 1}))
                        return false;
            }
    for (const auto& piece : pieces)
    {
        const Vec2 direction{std::cos(piece.angle), std::sin(piece.angle)};
        if (!p.part(piece.position, direction, -piece.size.x / 2, -piece.size.y / 2, piece.size.x,
                    piece.size.y, remainsColor(piece.owner)))
            return false;
        if (blood && !p.part(piece.position, direction, -2, -2, 4, 4, {.62f, .07f, .13f, 1}))
            return false;
    }
    for (const auto& player : players)
    {
        const Pose pose = effects ? effects->pose(player.id) : Pose{};
        const bool local = player.id == own;
        SDL_Color color = pose.hit > 0 ? SDL_Color{255, 230, 225, 255}
                          : local      ? SDL_Color{80, 175, 255, 255}
                                       : SDL_Color{230, 85, 85, 255};
        if (!player.alive())
            continue;
        float squash = pose.land > 0 ? std::sin(pose.land / .16f * 3.14159265f) * .14f : 0;
        float walk = player.grounded ? std::sin(pose.cycle) * .035f : 0;
        float stretch = !player.grounded ? std::clamp(-player.velocity.y / 620.f, -.08f, .08f) : 0;
        Rect body = player.bounds();
        body.size.x *= 1 + squash + walk - stretch;
        body.size.y *= 1 - squash - walk + stretch;
        body.position.x += (player.size.x - body.size.x) / 2;
        body.position.y += player.size.y - body.size.y;
        const int damageTier = (Player::maxHealth - player.health) / 25;
        if (!damageTier)
        {
            if (!p.rect(body, color))
                return false;
        }
        else
            for (int cell = 0; cell < 16; ++cell)
            {
                if ((damageTier >= 1 && cell == 12) || (damageTier >= 2 && cell == 15) ||
                    (damageTier >= 3 && cell == 13))
                    continue;
                if (!p.rect({{body.position.x + (cell % 4) * body.size.x / 4,
                              body.position.y + (cell / 4) * body.size.y / 4},
                             {body.size.x / 4 + .05f, body.size.y / 4 + .05f}},
                            color))
                    return false;
            }
        if (blood)
            for (const auto& wound : pose.wounds)
            {
                const Vec2 at{body.position.x + wound.local.x / 48 * body.size.x,
                              body.position.y + wound.local.y / 48 * body.size.y};
                if (!p.ellipse(at, wound.radius, wound.radius * .75f, {115, 17, 32, 255}, .3f) ||
                    !p.ellipse({at.x + 1, at.y - 1}, wound.radius * .6f, wound.radius * .4f,
                               {202, 33, 47, 255}))
                    return false;
            }
        // A small gaze mark gives the otherwise square body a clear facing direction.
        if (!p.rect({{body.position.x + (player.aimDirection.x < 0 ? 8.f : body.size.x - 12),
                      body.position.y + 12},
                     {4, 4}},
                    {24, 36, 52, 255}) ||
            !weapon(p, player, pose))
            return false;
        if (player.weapon.charging)
        {
            const float progress =
                1 - player.weapon.preparation / weaponDefinition(player.weapon.kind).preparation;
            if (!p.rect({{player.position.x, player.position.y - 21}, {48, 4}}, {55, 47, 25, 255}) ||
                !p.rect(
                    {{player.position.x, player.position.y - 21}, {48 * std::clamp(progress, 0.f, 1.f), 4}},
                    {255, 204, 91, 255}))
                return false;
        }
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
            if (particle.blood)
            {
                if (!p.ellipse(particle.position, particle.size, particle.size * .75f, {190, 24, 43, 255},
                               particle.angle))
                    return false;
            }
            else if (particle.chip)
            {
                if (!p.part(particle.position, {std::cos(particle.angle), std::sin(particle.angle)},
                            -particle.size / 2, -particle.size / 2, particle.size, particle.size,
                            remainsColor(particle.owner)))
                    return false;
            }
            else if (!p.rect({particle.position, {2, 2}}, {250, 196, 95, 255}))
                return false;
        }
    return !present || SDL_RenderPresent(renderer);
}

bool renderScene(SDL_Renderer* renderer, const World& world, bool present, const Effects* effects)
{
    return renderWorld(renderer, world.arena(), world.players(), world.bullets(), World::localPlayerId,
                       present, effects, world.debris().corpses(), world.debris().pieces());
}

bool renderSnapshot(SDL_Renderer* renderer, const WorldSnapshot& snapshot, PlayerId own,
                    const Effects* effects)
{
    return renderWorld(renderer, Arena::create(snapshot.map), snapshot.players, snapshot.bullets, own, false,
                       effects, snapshot.corpses, snapshot.pieces);
}
} // namespace cubeshot::client
