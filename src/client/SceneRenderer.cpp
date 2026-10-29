#include "client/SceneRenderer.h"
#include "client/Effects.h"
#include "client/VisualAssets.h"
#include "client/WorldView.h"
#include "game/Match.h"
#include "game/World.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace cubeshot::client
{
namespace
{
struct DrawSpan
{
    SDL_Texture* texture = nullptr;
    std::size_t start = 0, count = 0;
};

struct Painter
{
    SDL_Renderer* renderer;
    WorldView view;
    VisualAssets& assets;
    std::vector<SDL_Vertex>& vertices;
    std::vector<DrawSpan>& spans;

    void triangle(SDL_Vertex a, SDL_Vertex b, SDL_Vertex c, SDL_Texture* texture = nullptr) const
    {
        if (spans.empty() || spans.back().texture != texture)
            spans.push_back({texture, vertices.size(), 0});
        vertices.insert(vertices.end(), {a, b, c});
        spans.back().count += 3;
    }

    bool flush() const
    {
        for (const auto& span : spans)
            if (!SDL_RenderGeometry(renderer, span.texture, vertices.data() + span.start,
                                    static_cast<int>(span.count), nullptr, 0))
                return false;
        vertices.clear();
        spans.clear();
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

    bool rect(Rect r, SDL_Color color) const
    {
        return part(r.position, {1, 0}, 0, 0, r.size.x, r.size.y,
                    {color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f});
    }

    bool ellipse(Vec2 at, float rx, float ry, SDL_Color color, float angle = 0) const
    {
        static const auto circle = []
        {
            constexpr int count = 12;
            std::array<Vec2, count> points{};
            for (int n = 0; n < count; ++n)
            {
                float t = n * 6.2831853f / count;
                points[n] = {std::cos(t), std::sin(t)};
            }
            return points;
        }();
        float co = std::cos(angle), si = std::sin(angle);
        const SDL_FColor tint{color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f};
        auto vertex = [&](Vec2 v)
        {
            auto a =
                view.toScreen({at.x + v.x * rx * co - v.y * ry * si, at.y + v.x * rx * si + v.y * ry * co});
            return SDL_Vertex{{a.x, a.y}, tint, {0, 0}};
        };
        for (std::size_t i = 0; i < circle.size(); ++i)
            triangle(vertex({}), vertex(circle[i]), vertex(circle[(i + 1) % circle.size()]));
        return true;
    }

    bool line(Vec2 a, Vec2 b, float width, SDL_FColor color) const
    {
        float length = std::hypot(b.x - a.x, b.y - a.y);
        if (length < .001f)
            return true;
        return part(a, {(b.x - a.x) / length, (b.y - a.y) / length}, 0, -width / 2, length, width, color);
    }

    bool sprite(unsigned index, Vec2 centre, Vec2 size, float angle = 0,
                SDL_FColor color = {1, 1, 1, 1}) const
    {
        const float c = std::cos(angle), s = std::sin(angle), u = (index % 6 + .015f) / 6,
                    v = (index / 6 + .015f) / 4, du = .97f / 6, dv = .97f / 4;
        auto vertex = [&](float x, float y, float tu, float tv)
        {
            auto at = view.toScreen({centre.x + x * c - y * s, centre.y + x * s + y * c});
            return SDL_Vertex{{at.x, at.y}, color, {tu, tv}};
        };
        const auto a = vertex(-size.x / 2, -size.y / 2, u, v), b = vertex(size.x / 2, -size.y / 2, u + du, v),
                   d = vertex(-size.x / 2, size.y / 2, u, v + dv),
                   e = vertex(size.x / 2, size.y / 2, u + du, v + dv);
        triangle(a, b, e, assets.effects);
        triangle(a, e, d, assets.effects);
        return true;
    }

    bool material(unsigned index, const Polygon& shape, Vec2 origin, float angle, SDL_FColor color,
                  Vec2 uvOffset = {}) const
    {
        const auto indices = triangulate(shape);
        float c = std::cos(angle), s = std::sin(angle);
        auto vertex = [&](Vec2 local)
        {
            auto at =
                view.toScreen({origin.x + local.x * c - local.y * s, origin.y + local.x * s + local.y * c});
            const float u = (index % 4 + .01f + std::clamp((local.x + uvOffset.x) / 48, 0.f, 1.f) * .98f) / 4;
            const float v = (index / 4 + .01f + std::clamp((local.y + uvOffset.y) / 48, 0.f, 1.f) * .98f) / 2;
            return SDL_Vertex{{at.x, at.y}, color, {u, v}};
        };
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            triangle(vertex(shape[indices[i]]), vertex(shape[indices[i + 1]]), vertex(shape[indices[i + 2]]),
                     assets.materials);
        return true;
    }

    bool tile(unsigned index, Rect box, SDL_FColor color = {1, 1, 1, 1}) const
    {
        const float u = (index % 4 + .01f) / 4, v = (index / 4 + .01f) / 2, du = .98f / 4, dv = .98f / 2;
        auto vertex = [&](float x, float y, float tu, float tv)
        {
            auto at = view.toScreen({box.position.x + x, box.position.y + y});
            return SDL_Vertex{{at.x, at.y}, color, {tu, tv}};
        };
        auto a = vertex(0, 0, u, v), b = vertex(box.size.x, 0, u + du, v),
             c = vertex(box.size.x, box.size.y, u + du, v + dv), d = vertex(0, box.size.y, u, v + dv);
        triangle(a, b, c, assets.materials);
        triangle(a, c, d, assets.materials);
        return true;
    }

    bool outline(const Polygon& shape, Vec2 origin, float angle, float width, SDL_FColor color) const
    {
        const float c = std::cos(angle), s = std::sin(angle);
        auto world = [&](Vec2 v) { return Vec2{origin.x + v.x * c - v.y * s, origin.y + v.x * s + v.y * c}; };
        for (std::size_t i = 0; i < shape.size(); ++i)
            line(world(shape[i]), world(shape[(i + 1) % shape.size()]), width, color);
        return true;
    }
};

bool weapon(const Painter& p, const Player& player, const Pose& pose)
{
    Vec2 origin = player.weaponOrigin(), aim = player.aimDirection;
    const float recoil = pose.shot / .1f;
    origin.y += player.grounded ? std::sin(pose.cycle) *
                                      std::min(1.f, std::abs(player.velocity.x) / Player::moveSpeed) * .9f
                                : 0;
    origin.x -= aim.x * recoil * 6;
    origin.y -= aim.y * recoil * 6;
    const bool mirror = aim.x < 0;
    const auto kind = player.weapon.kind;
    const float length = player.weapon.barrel();
    if (!meleeWeapon(kind) && kind != WeaponKind::FireBoomerang && kind != WeaponKind::Rpg &&
        !p.part(origin, aim, 8, -7, length - 7, 14, {.04f, .07f, .11f, 1}, mirror))
        return false;
    if (meleeWeapon(kind))
    {
        const float swing = pose.shot > 0 ? std::sin(pose.shot / .1f * 3.14159f) * 1.4f - .7f : -.5f;
        const float angle = std::atan2(aim.y, aim.x) + (mirror ? -swing : swing);
        aim = {std::cos(angle), std::sin(angle)};
        const float drawn = kind == WeaponKind::Knife ? 26 : kind == WeaponKind::Katana ? 64 : 51;
        if (!p.part(origin, aim, -1, -5, 18, 10, {.04f, .06f, .08f, 1}, mirror) ||
            !p.part(origin, aim, 13, kind == WeaponKind::Bat ? -8.f : -4.f, drawn - 12,
                    kind == WeaponKind::Bat ? 16.f : 8.f, {.04f, .06f, .08f, 1}, mirror))
            return false;
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
    return pose.shot < .055f || p.sprite(18, player.muzzlePosition(), {30, 22},
                                         std::atan2(aim.y, aim.x) + 3.14159265f, {1, 1, 1, 1});
}

bool background(const Painter& p, const Arena& arena)
{
    const auto theme = mapInfo(arena.map).theme;
    const auto bounds = arena.bounds;
    const SDL_Color top = theme == MapTheme::Nature  ? SDL_Color{45, 79, 92, 255}
                          : theme == MapTheme::Ruins ? SDL_Color{66, 65, 83, 255}
                                                     : SDL_Color{38, 56, 79, 255};
    for (float y = 0; y < bounds.size.y; y += 16)
    {
        float t = y / bounds.size.y;
        p.rect({{bounds.position.x, bounds.position.y + y}, {bounds.size.x, 16}},
               {static_cast<Uint8>(top.r + t * 18), static_cast<Uint8>(top.g + t * 16),
                static_cast<Uint8>(top.b + t * 12), 255});
    }
    const SDL_Color quiet{static_cast<Uint8>(top.r + 5), static_cast<Uint8>(top.g + 6),
                          static_cast<Uint8>(top.b + 6), 255};
    for (float ratio : {.13f, .78f})
    {
        const float x = bounds.size.x * ratio;
        if (theme == MapTheme::Nature)
        {
            p.rect({{x, bounds.size.y * .42f}, {22, bounds.size.y * .58f}}, quiet);
            p.ellipse({x + 11, bounds.size.y * .4f}, 95, 75, quiet);
        }
        else if (theme == MapTheme::Ruins)
        {
            p.rect({{x, bounds.size.y * .4f}, {75, bounds.size.y * .6f}}, quiet);
            p.rect({{x - 6, bounds.size.y * .4f}, {88, 12}}, quiet);
        }
        else
        {
            p.rect({{x, bounds.size.y * .28f}, {18, bounds.size.y * .72f}}, quiet);
            p.rect({{x, bounds.size.y * .28f}, {105, 9}}, quiet);
        }
    }
    return true;
}

void staticScene(const Painter& p, const Arena& arena)
{
    const auto theme = mapInfo(arena.map).theme;
    background(p, arena);
    for (const auto& box : arena.platforms)
    {
        p.rect({{box.position.x + 3, box.position.y + 5}, box.size}, {12, 22, 33, 130});
        p.rect(box, {9, 19, 28, 255});
        const unsigned tile = theme == MapTheme::Nature ? 3 : theme == MapTheme::Ruins ? 1 : 0;
        p.tile(tile, {{box.position.x + 2, box.position.y + 2},
                      {std::max(0.f, box.size.x - 4), std::max(0.f, box.size.y - 4)}});
        if (theme == MapTheme::Nature)
            p.tile(2, {box.position, {box.size.x, 7}});
        else
            p.rect({{box.position.x + 2, box.position.y + 2}, {box.size.x - 4, 2}},
                   theme == MapTheme::Ruins ? SDL_Color{166, 161, 157, 255} : SDL_Color{143, 168, 186, 255});
    }
}

SDL_FColor ownerColor(PlayerId id, PlayerId own)
{
    return id == own ? SDL_FColor{.23f, .67f, .98f, 1} : SDL_FColor{.96f, .21f, .29f, 1};
}

void remains(const Painter& p, const PhysicsPiece& piece, PlayerId own, bool blood, bool fragment)
{
    if (piece.outline.empty())
        return;
    const auto color = ownerColor(piece.owner, own);
    p.ellipse({piece.position.x, piece.position.y + piece.size.y * .4f}, piece.size.x * .6f, 3,
              {7, 15, 25, 85});
    const SDL_FColor tint = piece.charred       ? SDL_FColor{.22f, .19f, .2f, 1}
                            : fragment && blood ? SDL_FColor{.92f, .7f, .7f, 1}
                                                : color;
    p.material(fragment && blood ? 5 : 4, piece.outline, piece.position, piece.angle, tint, piece.skinOffset);
    p.outline(piece.outline, piece.position, piece.angle, 2.2f, {.025f, .045f, .065f, 1});
    if (fragment)
    {
        for (std::size_t i = 0; i < piece.outline.size(); ++i)
        {
            auto a = piece.outline[i], b = piece.outline[(i + 1) % piece.outline.size()];
            const bool shell =
                (std::abs(a.x + piece.skinOffset.x) < .2f && std::abs(b.x + piece.skinOffset.x) < .2f) ||
                (std::abs(a.x + piece.skinOffset.x - 48) < .2f &&
                 std::abs(b.x + piece.skinOffset.x - 48) < .2f) ||
                (std::abs(a.y + piece.skinOffset.y) < .2f && std::abs(b.y + piece.skinOffset.y) < .2f) ||
                (std::abs(a.y + piece.skinOffset.y - 48) < .2f &&
                 std::abs(b.y + piece.skinOffset.y - 48) < .2f);
            if (shell)
                p.line(piecePoint(piece, a), piecePoint(piece, b), 4, color);
        }
    }
    const Vec2 eye{36 - piece.skinOffset.x, 14 - piece.skinOffset.y};
    if (containsPoint(piece.outline, eye))
    {
        const Vec2 direction{std::cos(piece.angle), std::sin(piece.angle)};
        p.part(piece.position, direction, eye.x - 3, eye.y - 3, 7, 7, {.95f, .95f, .9f, 1});
        p.part(piece.position, direction, eye.x, eye.y - 1, 3, 4, {.05f, .06f, .08f, 1});
    }
}
} // namespace

static bool renderWorld(SDL_Renderer* renderer, const Arena& arena, const std::vector<Player>& players,
                        const std::vector<Bullet>& bullets, PlayerId own, bool present,
                        const Effects* effects, const std::vector<Corpse>& corpses,
                        const std::vector<PhysicsPiece>& pieces, const std::vector<FirePatch>& fires)
{
    auto* assets = VisualAssets::get(renderer);
    if (!assets)
        return false;
    if (!SDL_SetRenderDrawColor(renderer, 32, 48, 68, 255) || !SDL_RenderClear(renderer) ||
        !SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND))
        return false;
    thread_local std::vector<SDL_Vertex> geometry;
    thread_local std::vector<DrawSpan> spans;
    geometry.clear();
    spans.clear();
    if (geometry.capacity() < 65536)
        geometry.reserve(65536);
    Painter p{renderer, WorldView(arena.bounds), *assets, geometry, spans};
    if (effects)
    {
        auto shake = effects->shakeOffset();
        p.view.offset.x += shake.x;
        p.view.offset.y += shake.y;
    }
    const float time = effects ? effects->time() : 0;
    const bool blood = !effects || effects->bloodVisible();

    struct StaticCache
    {
        std::uint64_t key = 0;
        std::vector<SDL_Vertex> vertices;
        std::vector<DrawSpan> spans;
    };

    thread_local StaticCache cached;
    std::uint64_t key = assets->identity;
    auto mix = [&](float value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        key = (key ^ bits) * 1099511628211ull;
    };
    mix(static_cast<float>(arena.map));
    mix(arena.bounds.position.x);
    mix(arena.bounds.position.y);
    mix(arena.bounds.size.x);
    mix(arena.bounds.size.y);
    for (const auto& box : arena.platforms)
    {
        mix(box.position.x);
        mix(box.position.y);
        mix(box.size.x);
        mix(box.size.y);
    }
    if (cached.key != key)
    {
        cached.key = key;
        cached.vertices.clear();
        cached.spans.clear();
        Painter backdrop{renderer, WorldView(arena.bounds), *assets, cached.vertices, cached.spans};
        staticScene(backdrop, arena);
    }
    geometry = cached.vertices;
    spans = cached.spans;
    if (effects)
    {
        const auto shake = effects->shakeOffset();
        if (shake.x != 0 || shake.y != 0)
            for (auto& v : geometry)
            {
                v.position.x += shake.x;
                v.position.y += shake.y;
            }
    }

    if (effects)
    {
        for (const auto& s : effects->stains())
        {
            float dark = std::min(1.f, s.age / 8);
            p.sprite(6 + s.variant, s.position, {s.width, s.height}, s.angle,
                     {1 - dark * .35f, .6f - dark * .25f, .65f - dark * .25f, .9f});
        }
        for (const auto& d : effects->drips())
        {
            p.line(d.position, {d.position.x + 1, d.position.y + d.length}, 2.5f, {.55f, .015f, .035f, .9f});
            p.ellipse({d.position.x + 1, d.position.y + d.length}, 2, 3, {170, 15, 33, 235});
        }
        for (const auto& smoke : effects->particles())
            if (smoke.smoke)
                p.sprite(12 + static_cast<unsigned>(std::clamp((1.2f - smoke.life) * 5, 0.f, 5.f)),
                         smoke.position, {smoke.size, smoke.size}, 0,
                         {1, 1, 1, std::clamp(smoke.life / 2.4f, 0.f, .4f)});
    }
    for (const auto& fire : fires)
        p.sprite(14 + static_cast<unsigned>(fire.id % 3), {fire.position.x, fire.position.y - 42},
                 {fire.radius * 1.5f, 62}, 0, {1, 1, 1, .2f});
    for (const auto& corpse : corpses)
        remains(p, corpse.body, own, blood, false);
    for (const auto& piece : pieces)
        remains(p, piece, own, blood, true);
    for (const auto& player : players)
    {
        if (!player.alive())
            continue;
        static const Pose empty;
        const auto& pose = effects ? effects->pose(player.id) : empty;
        auto color = ownerColor(player.id, own);
        const float hit = pose.hit / .12f * .3f;
        color.r += (1 - color.r) * hit;
        color.g += (1 - color.g) * hit;
        color.b += (1 - color.b) * hit;
        if (player.surface.charred)
        {
            color.r *= .55f;
            color.g *= .55f;
            color.b *= .55f;
        }
        const auto shape = player.bodyContour();
        p.ellipse({player.position.x + 24, player.position.y + 49}, 24, 3, {7, 17, 26, 90});
        p.material(4, shape, player.position, 0, color);
        p.outline(shape, player.position, 0, 1.7f, {.025f, .045f, .07f, 1});
        // Irregular exposed flesh belongs to the cut edges, never to an independent floating decal.
        if (blood)
            for (std::size_t i = 0; i < shape.size(); ++i)
            {
                auto a = shape[i], b = shape[(i + 1) % shape.size()];
                if (std::abs(a.x - b.x) > .1f && std::abs(a.y - b.y) > .1f)
                    p.line({player.position.x + a.x, player.position.y + a.y},
                           {player.position.x + b.x, player.position.y + b.y}, 1.2f, {.6f, .015f, .045f, 1});
            }
        const float eyeX = player.position.x + (player.aimDirection.x < 0 ? 7.f : 33.f),
                    eyeY = player.position.y + 11;
        p.rect({{eyeX - 1, eyeY - 1}, {10, 11}}, {12, 22, 32, 255});
        p.rect({{eyeX, eyeY}, {8, 9}}, {245, 247, 236, 255});
        p.rect({{eyeX + (player.aimDirection.x < 0 ? 0.f : 5.f), eyeY + 2}, {3, 6}}, {18, 29, 41, 255});
        weapon(p, player, pose);
        if (pose.shot > .055f &&
            (player.weapon.kind == WeaponKind::Minigun || player.weapon.kind == WeaponKind::Rpg))
            p.sprite(18, player.muzzlePosition(), {36, 26},
                     std::atan2(player.aimDirection.y, player.aimDirection.x) + 3.14159265f);
        if (meleeWeapon(player.weapon.kind) && pose.shot > 0)
        {
            const auto aim = player.aimDirection;
            const float reach = player.weapon.kind == WeaponKind::Knife ? 30.f : 48.f;
            p.sprite(19, {player.weaponOrigin().x + aim.x * reach, player.weaponOrigin().y + aim.y * reach},
                     {reach * 1.5f, reach * 1.2f}, std::atan2(aim.y, aim.x),
                     {1, 1, 1, std::clamp(pose.shot / .08f, 0.f, .85f)});
        }
        if (player.burning > 0)
            p.sprite(static_cast<unsigned>(time * 12 + player.id) % 6,
                     {player.position.x + 24, player.position.y + 29}, {45, 48}, 0, {1, 1, 1, .9f});
    }
    for (const auto& b : bullets)
    {
        const float speed = std::hypot(b.velocity.x, b.velocity.y);
        const Vec2 dir = speed > .001f ? Vec2{b.velocity.x / speed, b.velocity.y / speed} : Vec2{1, 0};
        if (b.weapon == WeaponKind::Flamethrower)
        {
            p.sprite(static_cast<unsigned>(time * 15 + b.id) % 6, b.position,
                     {20 + b.age * 25, 28 + b.age * 25}, std::atan2(dir.y, dir.x) + 1.5707963f,
                     {1, 1, 1, .9f});
            continue;
        }
        if (b.weapon == WeaponKind::FireBoomerang)
        {
            p.sprite(static_cast<unsigned>(time * 15 + b.id) % 6, b.position, {32, 32}, b.age * 15);
            p.part(b.position, {std::cos(b.age * 22), std::sin(b.age * 22)}, -13, -3, 26, 6,
                   {1, .85f, .4f, 1});
            continue;
        }
        if (b.weapon == WeaponKind::Rpg)
        {
            p.part(b.position, dir, -17, -6, 27, 12, {.04f, .08f, .1f, 1});
            p.part(b.position, dir, -14, -4, 22, 8, {.5f, .65f, .24f, 1});
            p.sprite(18, {b.position.x - dir.x * 22, b.position.y - dir.y * 22}, {23, 16},
                     std::atan2(dir.y, dir.x), {1, 1, 1, .85f});
            continue;
        }
        p.part(b.position, dir, -13, -1.5f, 13, 3, {1, .58f, .15f, .8f});
        p.ellipse(b.position, 3, 3, {255, 244, 167, 255});
    }
    for (const auto& fire : fires)
        p.sprite(static_cast<unsigned>(time * 12 + fire.id) % 6, {fire.position.x, fire.position.y - 20},
                 {fire.radius * 2.1f, 48}, 0, {1, 1, 1, std::min(1.f, fire.life * 3)});
    if (effects)
    {
        for (const auto& b : effects->blasts())
        {
            float age = 1 - b.life / .45f;
            for (int n = 0; n < 6; ++n)
            {
                float a = n * 6.2831853f / 6, spread = age * explosionRadius * .65f;
                p.sprite(static_cast<unsigned>(time * 18 + n) % 6,
                         {b.position.x + std::cos(a) * spread, b.position.y + std::sin(a) * spread},
                         {65 * (1 - age) + 20, 85 * (1 - age) + 20}, a, {1, 1, 1, 1 - age});
            }
        }
        for (const auto& splash : effects->splashes())
            p.sprite(6 + splash.variant, splash.position, {splash.size, splash.size * .65f}, splash.angle,
                     {1, 1, 1, std::min(1.f, splash.life * 8)});
        for (const auto& particle : effects->particles())
            if (particle.blood && !particle.chip && !particle.smoke)
            {
                float stretch =
                    std::clamp(std::hypot(particle.velocity.x, particle.velocity.y) / 150, 1.f, 2.2f);
                p.ellipse(particle.position, particle.size * stretch, particle.size * .7f, {195, 14, 35, 245},
                          std::atan2(particle.velocity.y, particle.velocity.x));
            }
        for (const auto& particle : effects->particles())
        {
            if (particle.smoke || (!particle.chip && particle.blood))
                continue;
            if (particle.chip)
            {
                auto tint = particle.blood ? SDL_FColor{1, 1, 1, 1} : ownerColor(particle.owner, own);
                if (particle.blood)
                    p.sprite(particle.sprite, particle.position, {particle.size, particle.size},
                             particle.angle, tint);
                else
                    p.part(particle.position, {std::cos(particle.angle), std::sin(particle.angle)},
                           -particle.size / 2, -particle.size / 3, particle.size, particle.size * .7f, tint);
            }
            else
                p.sprite(20, particle.position, {particle.size * 5, particle.size * 5}, particle.angle,
                         {1, 1, 1, std::min(1.f, particle.life * 3)});
        }
    }
    // All gameplay indicators are above smoke, blood and flames.
    for (const auto& player : players)
        if (player.alive())
        {
            const auto hp =
                Vec2{player.position.x, std::max(player.position.y - 12, arena.bounds.position.y + 1)};
            const float fraction = std::clamp(player.health, 0, 100) / 100.f;
            p.rect({{hp.x - 1, hp.y - 1}, {50, 8}}, {10, 20, 30, 255});
            p.rect({hp, {48, 6}}, {80, 35, 45, 255});
            p.rect({hp, {48 * fraction, 6}}, {85, 210, 125, 255});
            if (player.weapon.charging)
            {
                p.rect({{hp.x, hp.y - 9}, {48, 4}}, {45, 37, 22, 255});
                p.rect(
                    {{hp.x, hp.y - 9}, {48 * std::clamp(1 - player.weapon.preparation / .3f, 0.f, 1.f), 4}},
                    {255, 205, 80, 255});
            }
            if (player.weapon.kind == WeaponKind::Minigun)
            {
                p.rect({{hp.x, hp.y - 9}, {48, 4}}, {45, 37, 22, 255});
                p.rect({{hp.x, hp.y - 9}, {48 * player.weapon.heat, 4}},
                       player.weapon.overheated ? SDL_Color{255, 60, 25, 255} : SDL_Color{255, 195, 65, 255});
                p.rect({{hp.x, hp.y - 14}, {48 * player.weapon.spin, 3}}, {135, 196, 240, 255});
            }
        }
    const bool rendered = p.flush();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    return rendered && (!present || SDL_RenderPresent(renderer));
}

bool renderScene(SDL_Renderer* r, const World& world, bool present, const Effects* effects)
{
    return renderWorld(r, world.arena(), world.players(), world.bullets(), World::localPlayerId, present,
                       effects, world.debris().corpses(), world.debris().pieces(), world.fires());
}

bool renderSnapshot(SDL_Renderer* r, const WorldSnapshot& s, PlayerId own, const Effects* effects)
{
    thread_local Arena cached = Arena::create(s.map);
    if (cached.map != s.map)
        cached = Arena::create(s.map);
    return renderWorld(r, cached, s.players, s.bullets, own, false, effects, s.corpses, s.pieces, s.fires);
}
} // namespace cubeshot::client
