#include "game/Collision.h"
#include "game/TestSession.h"
#include "game/World.h"
#include <iostream>
#include <stdexcept>
using namespace cubeshot;

void expect(bool b, const char* reason)
{
    if (!b)
        throw std::runtime_error(reason);
}

int main()
{
    try
    {
        World world(Arena::create(MapId::FactoryPartitions, true), 4);
        TestConfiguration config;
        config.abilities[0].add(AbilityKind::SmallBody);
        config.abilities[0].add(AbilityKind::Armor);
        config.abilities[0].add(AbilityKind::Jetpack);
        config.weapons[0] = WeaponKind::Minigun;
        config.actives[0] = {ActiveKind::Barrier, ActiveKind::Dash};
        auto terrain = world.arena().terrainState();
        auto position = world.players()[0].position;
        expect(world.editTest({TestOperation::Build, 0}, config), "Live build applies safely");
        expect(world.players()[0].size.x == 24 && world.players()[0].weapon.kind == WeaponKind::Minigun,
               "Weapon and physical size change without recreating world");
        expect(world.arena().terrainState().health == terrain.health &&
                   world.players()[0].position.y + 24 == position.y + 48,
               "Build preserves map and feet");
        expect(world.players()[0].actives[0].kind == ActiveKind::Barrier, "Live build equips active slots");
        world.editTest({TestOperation::UseQ, 0}, config);
        world.update(1.f / 120);
        expect(world.players()[0].barrier > 0 && world.players()[0].actives[0].cooldown > 0,
               "Queued sandbox cast executes on resume");
        world.editTest({TestOperation::ReadyActives, 0}, config);
        expect(world.players()[0].actives[0].cooldown == 0, "Sandbox cooldown reset works immediately");
        for (auto op : {TestOperation::EmptyAmmo, TestOperation::Reload, TestOperation::FinishReload})
            expect(world.editTest({op, 0}, config), "Ammo commands accepted");
        expect(!world.players()[0].weapon.reloading && world.players()[0].weapon.ammo == 300,
               "Reload completes without advancing scene");
        world.editTest({TestOperation::Overheat, 0}, config);
        expect(world.players()[0].weapon.overheated, "Heat can be tested");
        world.editTest({TestOperation::Cool, 0}, config);
        expect(!world.players()[0].weapon.overheated, "Cooling restores weapon");
        world.editTest({TestOperation::EmptyShields, 0}, config);
        world.editTest({TestOperation::RefillShields, 0}, config);
        expect(world.players()[0].shields == 2, "Shields refill to ability capacity");
        config.healthPercent[0] = 10;
        world.editTest({TestOperation::Health, 0}, config);
        expect(world.players()[0].health == 10, "Current HP is editable");
        world.editTest({TestOperation::Heal, 0}, config);
        expect(world.players()[0].health == 100, "Heal affects live actor");
        world.editTest({TestOperation::EmptyFuel, 0}, config);
        expect(world.players()[0].jetpackFuel == 0, "Fuel is editable");
        world.editTest({TestOperation::RefillFuel, 0}, config);
        world.editTest({TestOperation::Respawn, 0}, config);
        expect(world.players()[0].health == 10 && world.players()[0].weapon.kind == WeaponKind::Minigun,
               "Respawn uses configured HP and loadout");
        world.editTest({TestOperation::RestoreMap, 0}, config);
        for (auto& p : world.players())
            for (auto solid : world.arena().platforms)
                expect(!collision::overlaps(p.bounds(), solid), "Map restoration relocates actors safely");
        World match(Arena::create(MapId::Duel), 2);
        expect(!match.editTest({TestOperation::Heal, 0}, config),
               "Sandbox commands cannot modify a live match");
        auto invalid = config;
        invalid.abilities[0].levels[0] = 100;
        expect(!world.editTest({TestOperation::Build, 0}, invalid), "Invalid levels rejected atomically");
        Arena tight{{{0, 0}, {1000, 540}},
                    {{{0, 440}, {1000, 100}}, {{100, 390}, {48, 20}}},
                    {{100, 392}, {400, 392}, {600, 392}, {800, 392}},
                    MapId::Test};
        World cramped(tight, 4);
        TestConfiguration small;
        small.abilities[0].add(AbilityKind::SmallBody);
        cramped.setAbilities(1, small.abilities[0]);
        auto before = cramped.players()[0].position;
        auto grown = small;
        grown.abilities[0] = {};
        expect(!cramped.editTest({TestOperation::Build, 0}, grown), "Growing into a ceiling is rejected");
        expect(cramped.players()[0].size.x == 24 && cramped.players()[0].position.y == before.y,
               "Rejected live edit preserves actor shape and location");
        Arena range{{{0, 0}, {1200, 540}},
                    {{{0, 440}, {1200, 100}}},
                    {{100, 392}, {600, 392}, {800, 392}, {1000, 392}},
                    MapId::Test};
        range.addTerrain({{350, 360}, {8, 80}}, Material::Glass);
        range.rebuildTerrain();
        World shooting(range, 4);
        auto pristine = shooting.arena().terrainState();
        shooting.equip(1, WeaponKind::Sniper);
        shooting.setPlayerInput(1, {0, false, true, {1, 0}, true});
        for (int i = 0; i < 120; ++i)
            shooting.update(1.f / 120);
        shooting.clearPlayerInput(1);
        expect(shooting.arena().terrainState().health != pristine.health,
               "Live test fixture has actual bullet destruction");
        expect(shooting.editTest({TestOperation::RestoreMap, 0}, TestConfiguration{}),
               "Damaged map restores live");
        expect(shooting.arena().terrainState().health == pristine.health && shooting.bullets().empty() &&
                   shooting.fires().empty() && shooting.debris().pieces().empty(),
               "Restoration repairs terrain and removes dangerous transient entities");
        World lethal(Arena{range.bounds,
                           range.fixedPlatforms.empty() ? std::vector<Rect>{{{0, 440}, {1200, 100}}}
                                                        : range.fixedPlatforms,
                           range.spawnPoints, MapId::Test},
                     4);
        lethal.equip(1, WeaponKind::Sniper);
        lethal.setPlayerInput(1, {0, false, true, {1, 0}, true});
        for (int i = 0; i < 120; ++i)
            lethal.update(1.f / 120);
        expect(!lethal.players()[1].alive(), "Respawn fixture contains an actually killed target");
        TestConfiguration restored;
        restored.weapons[1] = WeaponKind::Katana;
        restored.healthPercent[1] = 50;
        expect(lethal.editTest({TestOperation::Respawn, 1}, restored) && lethal.players()[1].health == 50 &&
                   lethal.players()[1].weapon.kind == WeaponKind::Katana && lethal.debris().corpses().empty(),
               "Dead target respawns once with configured HP/weapon and without its old corpse");
        std::cout << "Sandbox edits, state tools, safe restoration and match isolation passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
