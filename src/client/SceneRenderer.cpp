#include "client/SceneRenderer.h"
#include "client/Effects.h"
#include "client/WorldView.h"
#include "game/Match.h"
#include "game/World.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace cubeshot::client
{
namespace
{
struct Painter
{
    SDL_Renderer* renderer;
    WorldView view;
    std::vector<SDL_Vertex>& vertices;

    void triangle(SDL_Vertex a, SDL_Vertex b, SDL_Vertex c) const
    {
        vertices.push_back(a);
        vertices.push_back(b);
        vertices.push_back(c);
    }

    bool rect(Rect r, SDL_Color color) const
    {
        return part(r.position, {1, 0}, 0, 0, r.size.x, r.size.y,
                    {color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f});
    }

    bool ellipse(Vec2 at, float rx, float ry, SDL_Color color, float angle = 0) const
    {
        constexpr int sides = 12;
        static const auto circle = []
        {
            std::array<Vec2, sides> out{};
            for (int n = 0; n < sides; ++n)
            {
                const float t = n * 6.283185307f / sides;
                out[n] = {std::cos(t), std::sin(t)};
            }
            return out;
        }();
        const float c = std::cos(angle), sn = std::sin(angle);
        const SDL_FColor tint{color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f};
        auto vertex = [&](Vec2 local)
        {
            auto screen = view.toScreen(
                {at.x + local.x * rx * c - local.y * ry * sn, at.y + local.x * rx * sn + local.y * ry * c});
            return SDL_Vertex{{screen.x, screen.y}, tint, {0, 0}};
        };
        const auto centre = vertex({0, 0});
        for (int n = 0; n < sides; ++n)
            triangle(centre, vertex(circle[n]), vertex(circle[(n + 1) % sides]));
        return true;
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
        const auto a = vertex(x, y), b = vertex(x + w, y), c = vertex(x + w, y + h), d = vertex(x, y + h);
        triangle(a, b, c);
        triangle(a, c, d);
        return true;
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
    if (meleeWeapon(kind))
    {
        const float swing = pose.shot > 0 ? std::sin(pose.shot / .1f * 3.14159f) * 1.4f - .7f : -.5f;
        const float angle = std::atan2(aim.y, aim.x) + (mirror ? -swing : swing);
        aim = {std::cos(angle), std::sin(angle)};
        const float drawn = kind == WeaponKind::Knife ? 26 : kind == WeaponKind::Katana ? 64 : 51;
        return p.part(origin, aim, 0, -4, 16, 8, {.5f, .3f, .15f, 1}, mirror) &&
               p.part(origin, aim, 14, kind == WeaponKind::Bat ? -7.f : -3.f, drawn - 14,
                      kind == WeaponKind::Bat ? 14.f : 6.f,
                      kind == WeaponKind::Bat ? SDL_FColor{.7f, .46f, .2f, 1} : SDL_FColor{.85f, .92f, 1, 1},
                      mirror);
    }
    if (kind == WeaponKind::FireBoomerang)
        return p.part(origin, aim, 8, -4, 23, 7, {1, .36f, .05f, 1}, mirror) &&
               p.part(origin, aim, 24, -19, 7, 22, {1, .7f, .15f, 1}, mirror);
    if (kind == WeaponKind::Rpg)
        return p.part(origin, aim, 0, -10, length, 20, {.3f, .42f, .2f, 1}, mirror) &&
               p.part(origin, aim, 16, 8, 10, 15, {.3f, .24f, .16f, 1}, mirror) &&
               p.part(origin, aim, length - 13, -13, 18, 26, {.54f, .6f, .28f, 1}, mirror);
    if (kind == WeaponKind::Minigun)
    {
        const float spin = player.weapon.shots * 2.f + player.weapon.spin * 12;
        for (int n = 0; n < 3; ++n)
            if (!p.part(origin, aim, 13, -8 + n * 6.f + std::sin(spin + n * 2.1f) * 2, length - 13, 4,
                        player.weapon.overheated ? SDL_FColor{1, .3f, .1f, 1}
                                                 : SDL_FColor{.5f, .56f, .64f, 1},
                        mirror))
                return false;
        return p.part(origin, aim, 0, -12, 22, 24, {.25f, .3f, .37f, 1}, mirror);
    }
    if (kind == WeaponKind::Flamethrower)
        return p.part(origin, aim, 3, -9, 27, 18, {.7f, .32f, .12f, 1}, mirror) &&
               p.part(origin, aim, 25, -5, length - 20, 10, {.45f, .5f, .5f, 1}, mirror) &&
               p.ellipse(player.muzzlePosition(), 4, 4, {255, 150, 30, 255});
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
                        const std::vector<PhysicsPiece>& pieces, const std::vector<FirePatch>& fires)
{
    const auto theme = mapInfo(arena.map).theme;
    const SDL_Color bg = theme == MapTheme::Nature  ? SDL_Color{19, 32, 31, 255}
                         : theme == MapTheme::Ruins ? SDL_Color{28, 26, 36, 255}
                                                    : SDL_Color{24, 28, 38, 255};
    if (!SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a) || !SDL_RenderClear(renderer))
        return false;
    thread_local std::vector<SDL_Vertex> geometry;
    geometry.clear();
    if (geometry.capacity() < 65536)
        geometry.reserve(65536);
    Painter p{renderer, WorldView(arena.bounds), geometry};
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
        static const Pose emptyPose;
        const Pose& pose = effects ? effects->pose(player.id) : emptyPose;
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
        if (!player.damagedCells)
        {
            if (!p.rect(body, color))
                return false;
        }
        else
            for (int cell = 0; cell < 16; ++cell)
            {
                if (player.damagedCells & (1u << cell))
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
        if (player.burning > 0)
            for (int n = 0; n < 5; ++n)
            {
                const float flicker = std::sin(player.burning * 29 + n * 2.1f);
                if (!p.ellipse({player.position.x + 5 + n * 9.f, player.position.y + 36 + flicker * 4}, 6,
                               12 + flicker * 4, {255, 105, 20, 255}) ||
                    !p.ellipse({player.position.x + 5 + n * 9.f, player.position.y + 38}, 3, 7,
                               {255, 215, 65, 255}))
                    return false;
            }
        if (player.weapon.kind == WeaponKind::Minigun)
        {
            if (!p.rect({{player.position.x, player.position.y - 21}, {48, 4}}, {45, 38, 30, 255}) ||
                !p.rect({{player.position.x, player.position.y - 21}, {48 * player.weapon.heat, 4}},
                        player.weapon.overheated ? SDL_Color{255, 65, 35, 255}
                                                 : SDL_Color{255, 190, 50, 255}) ||
                !p.rect({{player.position.x, player.position.y - 27}, {48 * player.weapon.spin, 3}},
                        {150, 200, 245, 255}))
                return false;
        }
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
    for (const auto& fire : fires)
        for (int n = 0; n < 5; ++n)
        {
            const float flicker = std::sin(fire.life * 24 + n * 2 + fire.id);
            const Vec2 at{fire.position.x + (n - 2) * fire.radius * .4f, fire.position.y - 6};
            if (!p.ellipse(at, fire.radius * .28f, 10 + flicker * 4, {240, 75, 15, 255}) ||
                !p.ellipse({at.x, at.y + 3}, fire.radius * .14f, 6 + flicker * 2, {255, 205, 50, 255}))
                return false;
        }
    for (const auto& bullet : bullets)
    {
        const float speed = std::hypot(bullet.velocity.x, bullet.velocity.y);
        const Vec2 dir = speed > 0 ? Vec2{bullet.velocity.x / speed, bullet.velocity.y / speed} : Vec2{1, 0};
        if (bullet.weapon == WeaponKind::Flamethrower)
        {
            if (!p.ellipse(bullet.position, 8 + bullet.age * 14, 5 + bullet.age * 10, {255, 110, 15, 255}) ||
                !p.ellipse(bullet.position, 4, 3, {255, 225, 95, 255}))
                return false;
            continue;
        }
        if (bullet.weapon == WeaponKind::FireBoomerang)
        {
            const Vec2 rotation{std::cos(bullet.age * 22), std::sin(bullet.age * 22)};
            if (!p.ellipse(bullet.position, 17, 17, {240, 90, 15, 255}) ||
                !p.part(bullet.position, rotation, -13, -3, 26, 6, {1, .8f, .25f, 1}) ||
                !p.part(bullet.position, rotation, 7, -12, 6, 15, {1, .9f, .5f, 1}))
                return false;
            continue;
        }
        if (bullet.weapon == WeaponKind::Rpg)
        {
            if (!p.part(bullet.position, dir, -16, -5, 25, 10, {.55f, .65f, .3f, 1}) ||
                !p.part(bullet.position, dir, -25, -3, 10, 6, {1, .4f, .08f, 1}))
                return false;
            continue;
        }
        if (!p.part(bullet.position, dir, -15, -1.5f, 15, 3, {1, .55f, .12f, 1}) ||
            !p.rect({{bullet.position.x - Bullet::radius, bullet.position.y - Bullet::radius},
                     {Bullet::radius * 2, Bullet::radius * 2}},
                    {255, 240, 150, 255}))
            return false;
    }
    if (effects)
        for (const auto& blast : effects->blasts())
        {
            const float age = 1 - blast.life / .45f, radius = 15 + age * explosionRadius;
            for (int n = 0; n < 12; ++n)
            {
                const float a = n * 6.2831853f / 12;
                if (!p.ellipse(
                        {blast.position.x + std::cos(a) * radius, blast.position.y + std::sin(a) * radius},
                        8 + blast.life * 45, 8 + blast.life * 45,
                        {255, static_cast<Uint8>(100 + blast.life * 220), 35, 255}))
                    return false;
            }
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
    if (!geometry.empty() && !SDL_RenderGeometry(renderer, nullptr, geometry.data(),
                                                 static_cast<int>(geometry.size()), nullptr, 0))
        return false;
    return !present || SDL_RenderPresent(renderer);
}

bool renderScene(SDL_Renderer* renderer, const World& world, bool present, const Effects* effects)
{
    return renderWorld(renderer, world.arena(), world.players(), world.bullets(), World::localPlayerId,
                       present, effects, world.debris().corpses(), world.debris().pieces(), world.fires());
}

bool renderSnapshot(SDL_Renderer* renderer, const WorldSnapshot& snapshot, PlayerId own,
                    const Effects* effects)
{
    thread_local Arena cachedArena = Arena::create(snapshot.map);
    if (cachedArena.map != snapshot.map)
        cachedArena = Arena::create(snapshot.map);
    return renderWorld(renderer, cachedArena, snapshot.players, snapshot.bullets, own, false, effects,
                       snapshot.corpses, snapshot.pieces, snapshot.fires);
}
} // namespace cubeshot::client
