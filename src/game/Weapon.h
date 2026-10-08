#pragma once

namespace cubeshot {

struct Weapon {
    int damage = 10;
    float bulletSpeed = 600.0f;
    float shotInterval = 0.25f; // Секунды между выстрелами.
    float cooldown = 0.0f;     // Секунды до разрешённого выстрела.

    void update(float deltaSeconds);
};

} // namespace cubeshot
