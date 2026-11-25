#include "game/Modes.h"

namespace cubeshot
{
std::u32string_view modeName(GameMode m)
{
    static constexpr std::u32string_view n[]{U"Дуэль", U"Гонка вооружений", U"Контроль точки", U"Хаос"};
    return unsigned(m) < 4 ? n[unsigned(m)] : U"Дуэль";
}

std::u32string_view chaosName(ChaosKind k)
{
    static constexpr std::u32string_view n[]{U"Обычные условия",     U"Низкая гравитация", U"Быстрые снаряды",
                                             U"Быстрая перезарядка", U"Сильная отдача",    U"Хрупкие укрытия",
                                             U"Быстрое движение"};
    return unsigned(k) < 7 ? n[unsigned(k)] : n[0];
}

SimulationModifiers chaosModifiers(ChaosKind k)
{
    SimulationModifiers m;
    switch (k)
    {
        case ChaosKind::LowGravity:
            m.gravity = .65f;
            break;
        case ChaosKind::FastProjectiles:
            m.projectiles = 1.25f;
            break;
        case ChaosKind::FastReload:
            m.reload = .7f;
            break;
        case ChaosKind::HeavyRecoil:
            m.recoil = 1.5f;
            break;
        case ChaosKind::FragileCover:
            m.terrain = 1.5f;
            break;
        case ChaosKind::FastMovement:
            m.movement = 1.2f;
            break;
        default:
            break;
    }
    return m;
}

std::vector<WeaponKind> armsChain(unsigned count)
{
    const std::vector<WeaponKind> all{WeaponKind::Pistol,
                                      WeaponKind::Smg,
                                      WeaponKind::Automatic,
                                      WeaponKind::Shotgun,
                                      WeaponKind::Revolver,
                                      WeaponKind::Rifle,
                                      WeaponKind::Sniper,
                                      WeaponKind::Minigun,
                                      WeaponKind::Crossbow,
                                      WeaponKind::Electric,
                                      WeaponKind::Beam,
                                      WeaponKind::Disc,
                                      WeaponKind::GrenadeLauncher,
                                      WeaponKind::StickyLauncher,
                                      WeaponKind::Rpg,
                                      WeaponKind::Flamethrower,
                                      WeaponKind::FireBoomerang,
                                      WeaponKind::Bat,
                                      WeaponKind::Hammer,
                                      WeaponKind::Katana,
                                      WeaponKind::Knife};
    if (count == 21)
        return all;
    std::vector<WeaponKind> out;
    if (count == 7)
    {
        for (unsigned i : {1, 4, 6, 9, 15, 20, 21})
            out.push_back(all[i - 1]);
    }
    else
    {
        for (unsigned i : {1, 2, 3, 4, 5, 7, 8, 10, 12, 13, 14, 15, 19, 21})
            out.push_back(all[i - 1]);
    }
    return out;
}
} // namespace cubeshot
