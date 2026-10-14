#include "client/BitmapFont.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>

namespace cubeshot::client {
namespace {

struct Glyph { char32_t letter; std::array<std::uint8_t, 7> rows; };
// Собственный шрифт 5 x 7: каждый бит задаёт один пиксель строки.
constexpr Glyph glyphs[]{
    {U'A', {14,17,17,31,17,17,17}}, {U'B', {30,17,17,30,17,17,30}},
    {U'C', {14,17,16,16,16,17,14}}, {U'D', {30,17,17,17,17,17,30}},
    {U'E', {31,16,16,30,16,16,31}}, {U'F', {31,16,16,30,16,16,16}},
    {U'G', {14,17,16,23,17,17,14}}, {U'H', {17,17,17,31,17,17,17}},
    {U'I', {14,4,4,4,4,4,14}}, {U'J', {7,2,2,2,2,18,12}},
    {U'K', {17,18,20,24,20,18,17}}, {U'L', {16,16,16,16,16,16,31}},
    {U'M', {17,27,21,21,17,17,17}}, {U'N', {17,25,25,21,19,19,17}},
    {U'O', {14,17,17,17,17,17,14}}, {U'P', {30,17,17,30,16,16,16}},
    {U'Q', {14,17,17,17,21,18,13}}, {U'R', {30,17,17,30,20,18,17}},
    {U'S', {15,16,16,14,1,1,30}}, {U'T', {31,4,4,4,4,4,4}},
    {U'U', {17,17,17,17,17,17,14}}, {U'V', {17,17,17,17,17,10,4}},
    {U'W', {17,17,17,21,21,21,10}}, {U'X', {17,17,10,4,10,17,17}},
    {U'Y', {17,17,10,4,4,4,4}}, {U'Z', {31,1,2,4,8,16,31}},
    {U'0', {14,17,19,21,25,17,14}}, {U'1', {4,12,4,4,4,4,14}},
    {U'2', {14,17,1,2,4,8,31}}, {U'3', {30,1,1,14,1,1,30}},
    {U'4', {2,6,10,18,31,2,2}}, {U'5', {31,16,16,30,1,1,30}},
    {U'6', {14,16,16,30,17,17,14}}, {U'7', {31,1,2,4,8,8,8}},
    {U'8', {14,17,17,14,17,17,14}}, {U'9', {14,17,17,15,1,1,14}},
    {U'Б', {31,16,16,30,17,17,30}}, {U'Г', {31,16,16,16,16,16,16}},
    {U'Д', {14,10,10,10,10,31,17}}, {U'Ё', {10,0,31,16,30,16,31}},
    {U'Ж', {21,21,21,14,21,21,21}}, {U'З', {14,17,1,6,1,17,14}},
    {U'И', {17,17,19,21,25,17,17}}, {U'Й', {10,4,17,19,21,25,17}},
    {U'Л', {7,9,9,9,9,9,17}}, {U'П', {31,17,17,17,17,17,17}},
    {U'У', {17,17,17,15,1,1,14}}, {U'Ф', {4,14,21,21,14,4,4}},
    {U'Ц', {18,18,18,18,18,31,1}}, {U'Ч', {17,17,17,15,1,1,1}},
    {U'Ш', {21,21,21,21,21,21,31}}, {U'Щ', {21,21,21,21,21,31,1}},
    {U'Ъ', {24,8,8,14,9,9,14}}, {U'Ы', {17,17,17,29,21,21,29}},
    {U'Ь', {16,16,16,30,17,17,30}}, {U'Э', {14,17,1,7,1,17,14}},
    {U'Ю', {18,21,21,29,21,21,18}}, {U'Я', {15,17,17,15,5,9,17}},
    {U' ', {0,0,0,0,0,0,0}}, {U'.', {0,0,0,0,0,12,12}},
    {U'-', {0,0,0,31,0,0,0}}, {U'/', {1,2,2,4,8,8,16}},
    {U':', {0,12,12,0,12,12,0}}, {U'_', {0,0,0,0,0,0,31}},
    {U'?', {14,17,1,2,4,0,4}}, {U',', {0,0,0,0,0,4,8}}
};

char32_t fontLetter(char32_t letter)
{
    if (letter >= U'a' && letter <= U'z') letter -= U'a' - U'A';
    if (letter >= U'а' && letter <= U'я') letter -= U'а' - U'А';
    if (letter == U'ё') letter = U'Ё';
    switch (letter) {
    case U'А': return U'A'; case U'В': return U'B'; case U'Е': return U'E';
    case U'К': return U'K'; case U'М': return U'M'; case U'Н': return U'H';
    case U'О': return U'O'; case U'Р': return U'P'; case U'С': return U'C';
    case U'Т': return U'T'; case U'Х': return U'X'; case U'—': return U'-';
    default: return letter;
    }
}

const Glyph& glyph(char32_t letter)
{
    letter = fontLetter(letter);
    for (const Glyph& candidate : glyphs) {
        if (candidate.letter == letter) return candidate;
    }
    return glyph(U'?');
}

} // namespace

float textWidth(std::u32string_view text, int scale)
{
    return text.empty() ? 0.0f : static_cast<float>((text.size() * 6 - 1) * scale);
}

bool drawText(SDL_Renderer* renderer, std::u32string_view text, Vec2 position,
              int scale, SDL_Color color)
{
    if (!SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a)) return false;
    for (char32_t letter : text) {
        const auto& rows = glyph(letter).rows;
        for (int y = 0; y < 7; ++y) {
            for (int x = 0; x < 5; ++x) {
                if ((rows[y] & (1 << (4 - x))) != 0) {
                    const SDL_FRect pixel{position.x + x * scale, position.y + y * scale,
                                          static_cast<float>(scale), static_cast<float>(scale)};
                    if (!SDL_RenderFillRect(renderer, &pixel)) return false;
                }
            }
        }
        position.x += 6 * scale;
    }
    return true;
}

} // namespace cubeshot::client
