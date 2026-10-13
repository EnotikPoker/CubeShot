#pragma once

namespace cubeshot {

struct Weapon {
    static constexpr float barrelLength = 36.0f;

    int damage = 100;
    float bulletSpeed = 800.0f;
    float shotInterval = 0.25f; // Секунды между выстрелами.
    float cooldown = 0.0f;     // Секунды до разрешённого выстрела.

    void update(float deltaSeconds);
    bool tryFire();
};

} // namespace cubeshot
