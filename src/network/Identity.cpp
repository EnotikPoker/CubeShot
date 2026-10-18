#include "network/Identity.h"
#include <openssl/rand.h>
#include <stdexcept>

namespace cubeshot::network
{
std::u32string nicknameText(std::string_view value)
{
    std::u32string out;
    for (std::size_t i = 0; i < value.size();)
    {
        const auto first = static_cast<unsigned char>(value[i++]);
        char32_t c = first;
        if (first >= 0xc2 && first <= 0xdf)
        {
            if (i >= value.size())
                return {};
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xc0) != 0x80)
                return {};
            c = ((first & 0x1f) << 6) | (next & 0x3f);
        }
        else if (first >= 0x80)
            return {};
        if (!((c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') || (c >= U'А' && c <= U'я') || c == U'Ё' ||
              c == U'ё' || (c >= U'0' && c <= U'9') || c == U' ' || c == U'_' || c == U'-'))
            return {};
        out.push_back(c);
    }
    return out;
}

bool validNickname(std::string_view name)
{
    if (name.empty() || name.size() > 32)
        return false;
    const auto text = nicknameText(name);
    return !text.empty() && text.size() <= 16 && text.find_first_not_of(U' ') != std::u32string::npos;
}

std::string randomToken()
{
    unsigned char bytes[32];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
        throw std::runtime_error("Cannot create reservation token");
    constexpr char hex[] = "0123456789abcdef";
    std::string token;
    for (auto b : bytes)
    {
        token += hex[b >> 4];
        token += hex[b & 15];
    }
    return token;
}
} // namespace cubeshot::network
