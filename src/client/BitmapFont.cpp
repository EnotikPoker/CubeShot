#include "client/BitmapFont.h"
#include "client/FontAtlas.h"
#include "client/VisualAssets.h"
#include <SDL3/SDL.h>
#include <vector>

namespace cubeshot::client
{
namespace
{
float factor(int scale)
{
    const float size = scale <= 1   ? 10.f
                       : scale == 2 ? 18.f
                       : scale == 3 ? 26.f
                       : scale == 4 ? 34.f
                       : scale == 5 ? 42.f
                                    : scale * 8.f;
    return size / 32;
}

const FontGlyph& glyph(char32_t code, unsigned weight)
{
    for (const auto& g : fontGlyphs)
        if (g.code == static_cast<unsigned>(code) && g.weight == weight)
            return g;
    for (const auto& g : fontGlyphs)
        if (g.code == '?' && g.weight == weight)
            return g;
    return fontGlyphs.front();
}
} // namespace

float textWidth(std::u32string_view text, int scale)
{
    float width = 0;
    for (auto letter : text)
        width += glyph(letter, scale >= 3 ? 1 : 0).advance * factor(scale);
    return width;
}

float textHeight(int scale)
{
    return 38 * factor(scale);
}

bool drawText(SDL_Renderer* r, std::u32string_view text, Vec2 at, int scale, SDL_Color color)
{
    if (text.empty())
        return true;
    auto* assets = VisualAssets::get(r);
    if (!assets)
        return false;
    const float f = factor(scale);
    float w = 0, h = 0;
    SDL_GetTextureSize(assets->font, &w, &h);
    std::vector<SDL_Vertex> vertices;
    vertices.reserve(text.size() * 6);
    const SDL_FColor tint{color.r / 255.f, color.g / 255.f, color.b / 255.f, color.a / 255.f};
    for (auto letter : text)
    {
        const auto& g = glyph(letter, scale >= 3 ? 1 : 0);
        if (g.width > 0 && g.height > 0)
        {
            const float x = at.x + g.left * f, y = at.y + g.top * f, dx = g.width * f, dy = g.height * f;
            const float u = g.x / w, v = g.y / h, du = g.width / w, dv = g.height / h;
            const SDL_Vertex a{{x, y}, tint, {u, v}}, b{{x + dx, y}, tint, {u + du, v}},
                c{{x + dx, y + dy}, tint, {u + du, v + dv}}, d{{x, y + dy}, tint, {u, v + dv}};
            vertices.insert(vertices.end(), {a, b, c, a, c, d});
        }
        at.x += g.advance * f;
    }
    return vertices.empty() || SDL_RenderGeometry(r, assets->font, vertices.data(),
                                                  static_cast<int>(vertices.size()), nullptr, 0);
}
} // namespace cubeshot::client
