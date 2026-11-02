#include "client/TextRenderer.h"
#include "client/VisualAssets.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cubeshot::client
{
namespace
{
float fontSize(int scale)
{
    return scale <= 1   ? 14.f
           : scale == 2 ? 20.f
           : scale == 3 ? 28.f
           : scale == 4 ? 36.f
           : scale == 5 ? 44.f
                        : scale * 8.f;
}

std::string utf8(std::u32string_view value)
{
    std::string out;
    for (auto c : value)
    {
        if (c < 0x80)
            out.push_back(static_cast<char>(c));
        else if (c < 0x800)
        {
            out.push_back(static_cast<char>(0xc0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 63)));
        }
        else if (c < 0x10000)
        {
            out.push_back(static_cast<char>(0xe0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (c & 63)));
        }
        else
        {
            out.push_back(static_cast<char>(0xf0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 63)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63)));
            out.push_back(static_cast<char>(0x80 | (c & 63)));
        }
    }
    return out;
}
} // namespace

TextRenderer::TextRenderer(SDL_Renderer* r) : renderer_(r)
{
    if (!TTF_Init())
        throw std::runtime_error(SDL_GetError());
    initialized_ = true;
    engine_ = TTF_CreateRendererTextEngine(r);
    if (!engine_)
    {
        TTF_Quit();
        throw std::runtime_error(SDL_GetError());
    }
    try
    {
        font(2);
    }
    catch (...)
    {
        TTF_DestroyRendererTextEngine(engine_);
        TTF_Quit();
        throw;
    }
}

void TextRenderer::clearText()
{
    for (auto& item : texts_)
        TTF_DestroyText(item.second);
    texts_.clear();
}

TextRenderer::~TextRenderer()
{
    clearText();
    for (auto& item : fonts_)
        TTF_CloseFont(item.second);
    TTF_DestroyRendererTextEngine(engine_);
    if (initialized_)
        TTF_Quit();
}

float TextRenderer::outputScale(SDL_FRect* result) const
{
    int w = 960, h = 540;
    SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode);
    SDL_FRect area{0, 0, static_cast<float>(w), static_cast<float>(h)};
    if (mode != SDL_LOGICAL_PRESENTATION_DISABLED)
        SDL_GetRenderLogicalPresentationRect(renderer_, &area);
    if (result)
        *result = area;
    return mode == SDL_LOGICAL_PRESENTATION_DISABLED
               ? 1.f
               : std::max(.1f, std::min(area.w / std::max(1, lw), area.h / std::max(1, lh)));
}

TTF_Font* TextRenderer::font(int scale)
{
    const auto key = std::make_pair(
        std::max(8, static_cast<int>(std::lround(fontSize(scale) * outputScale()))), scale >= 3);
    auto found = fonts_.find(key);
    if (found != fonts_.end())
        return found->second;
    if (fonts_.size() >= 24)
    {
        clearText();
        for (auto& item : fonts_)
            TTF_CloseFont(item.second);
        fonts_.clear();
    }
    auto* value = TTF_OpenFont((VisualAssets::directory() / "fonts" / "Rubik.ttf").u8string().c_str(),
                               static_cast<float>(key.first));
    if (!value)
        throw std::runtime_error(SDL_GetError());
    TTF_SetFontStyle(value, key.second ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL);
    fonts_.emplace(key, value);
    return value;
}

TTF_Text* TextRenderer::text(std::u32string_view value, int scale)
{
    auto* face = font(scale);
    const auto bytes = utf8(value);
    const auto key =
        std::make_tuple(static_cast<int>(std::lround(fontSize(scale) * outputScale())), scale >= 3, bytes);
    auto found = texts_.find(key);
    if (found != texts_.end())
        return found->second;
    if (texts_.size() >= 256)
        clearText();
    auto* result = TTF_CreateText(engine_, face, bytes.c_str(), bytes.size());
    if (!result)
        throw std::runtime_error(SDL_GetError());
    texts_.emplace(key, result);
    return result;
}

float TextRenderer::width(std::u32string_view value, int scale)
{
    if (value.empty())
        return 0;
    int w = 0, h = 0;
    if (!TTF_GetTextSize(text(value, scale), &w, &h))
        return 0;
    return w / outputScale();
}

float TextRenderer::height(int scale)
{
    return TTF_GetFontHeight(font(scale)) / outputScale();
}

bool TextRenderer::draw(std::u32string_view value, Vec2 at, int scale, SDL_Color color)
{
    if (value.empty())
        return true;
    SDL_FRect area;
    const float ratio = outputScale(&area);
    auto* item = text(value, scale);
    TTF_SetTextColor(item, color.r, color.g, color.b, color.a);
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    SDL_GetRenderLogicalPresentation(renderer_, &lw, &lh, &mode);
    SDL_Rect viewport, clip;
    SDL_GetRenderViewport(renderer_, &viewport);
    SDL_GetRenderClipRect(renderer_, &clip);
    const bool clipped = SDL_RenderClipEnabled(renderer_);
    float sx = 1, sy = 1;
    SDL_GetRenderScale(renderer_, &sx, &sy);
    bool ok = SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED) &&
              SDL_SetRenderViewport(renderer_, nullptr) && SDL_SetRenderScale(renderer_, 1, 1);
    if (clipped)
    {
        const SDL_Rect physical{static_cast<int>(std::floor(area.x + clip.x * ratio)),
                                static_cast<int>(std::floor(area.y + clip.y * ratio)),
                                static_cast<int>(std::ceil(clip.w * ratio)),
                                static_cast<int>(std::ceil(clip.h * ratio))};
        ok = SDL_SetRenderClipRect(renderer_, &physical) && ok;
    }
    else
        ok = SDL_SetRenderClipRect(renderer_, nullptr) && ok;
    ok = TTF_DrawRendererText(item, std::round(area.x + at.x * ratio), std::round(area.y + at.y * ratio)) &&
         ok;
    // Restore every transform/clip changed by this physical-pixel text pass.
    ok = SDL_SetRenderLogicalPresentation(renderer_, lw, lh, mode) && ok;
    ok = SDL_SetRenderScale(renderer_, sx, sy) && ok;
    ok = SDL_SetRenderViewport(renderer_, &viewport) && ok;
    ok = SDL_SetRenderClipRect(renderer_, clipped ? &clip : nullptr) && ok;
    return ok;
}
} // namespace cubeshot::client
