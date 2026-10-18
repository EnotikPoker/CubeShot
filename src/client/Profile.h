#pragma once
#include <string>
namespace cubeshot::client {
struct Profile { std::string nickname="Игрок",directoryUrl; };
Profile loadProfile();
void saveProfile(const Profile& profile);
}
