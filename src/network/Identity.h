#pragma once
#include <string>
#include <string_view>
namespace cubeshot::network {
bool validNickname(std::string_view name);
std::u32string nicknameText(std::string_view name);
std::string randomToken();
}
