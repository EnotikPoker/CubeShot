#include "client/UiText.h"
#include "client/VisualAssets.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace cubeshot::client;

void expect(bool b, const char* why)
{
    if (!b)
        throw std::runtime_error(why);
}

int main()
{
    if (!SDL_Init(0))
        return 1;
    try
    {
        float reference = 0;
        for (int height : {720, 1080, 1440, 2160})
        {
            const int width = height * 16 / 9;
            std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
                SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
            std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
                SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
            VisualAssets assets(renderer.get());
            expect(SDL_SetRenderLogicalPresentation(renderer.get(), 960, 540,
                                                    SDL_LOGICAL_PRESENTATION_LETTERBOX),
                   "Logical UI canvas is enabled");
            SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
            SDL_RenderClear(renderer.get());
            const SDL_Rect clip{70, 120, 180, 60};
            SDL_SetRenderClipRect(renderer.get(), &clip);
            const float logicalWidth = textWidth(renderer.get(), U"Сменить оружие", 2);
            expect(logicalWidth > 120 && logicalWidth < 240, "Cyrillic metrics remain readable at every DPI");
            if (!reference)
                reference = logicalWidth;
            expect(std::abs(reference - logicalWidth) < 5,
                   "Text measurements stay in logical units across 720p through 4K");
            expect(drawText(renderer.get(), U"Сменить оружие — Ёж", {80, 130}, 2, {255, 255, 255, 255}),
                   "Physical-pixel renderer draws Unicode text");
            SDL_Rect after;
            SDL_GetRenderClipRect(renderer.get(), &after);
            int lw = 0, lh = 0;
            SDL_RendererLogicalPresentation mode;
            SDL_GetRenderLogicalPresentation(renderer.get(), &lw, &lh, &mode);
            expect(SDL_RenderClipEnabled(renderer.get()) && after.x == clip.x && after.y == clip.y &&
                       after.w == clip.w && after.h == clip.h && lw == 960 && lh == 540 &&
                       mode == SDL_LOGICAL_PRESENTATION_LETTERBOX,
                   "Text restores clipping and logical presentation exactly");
            expect(SDL_RenderPresent(renderer.get()), "Flush physical text before reading pixels");
            const float factor = height / 540.f;
            unsigned bright = 0;
            for (int y = static_cast<int>(130 * factor); y < static_cast<int>(165 * factor); ++y)
                for (int x = static_cast<int>(80 * factor); x < static_cast<int>(240 * factor); ++x)
                {
                    Uint8 r, g, b, a;
                    SDL_ReadSurfacePixel(surface.get(), x, y, &r, &g, &b, &a);
                    bright += r > 200;
                }
            expect(bright > 40, "Cyrillic text actually appears in the expected physical-pixel region");
            Uint8 r, g, b, a;
            SDL_ReadSurfacePixel(surface.get(), static_cast<int>(260 * factor),
                                 static_cast<int>(145 * factor), &r, &g, &b, &a);
            expect(r == 0 && g == 0 && b == 0,
                   "Physical-pixel text cannot escape the logical clipping region");
            SDL_SetRenderClipRect(renderer.get(), nullptr);
            expect(drawText(renderer.get(), U"CubeShot", {80, 40}, 8, {240, 245, 250, 255}),
                   "Large title uses a freshly rasterized font, not enlarged atlas glyphs");
            SDL_RenderPresent(renderer.get());
            const char* capture = SDL_getenv("CUBESHOT_CAPTURE_DIR");
            if (capture)
            {
                auto name = std::string(capture) + "/text-" + std::to_string(height) + ".png";
                SDL_SavePNG(surface.get(), name.c_str());
            }
        }
        std::cout << "DPI text, Cyrillic, clipping and renderer-state tests passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        SDL_Quit();
        return 1;
    }
    SDL_Quit();
    return 0;
}
