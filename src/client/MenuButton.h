#pragma once
#include "game/Types.h"
#include <string_view>

namespace cubeshot::client
{
struct MenuButton
{
    std::u32string_view label;
    Rect bounds;
    bool placeholder = false;
};
} // namespace cubeshot::client
