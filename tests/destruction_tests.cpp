#include "game/BotController.h"
#include "game/Collision.h"
#include "game/Match.h"
#include "game/Prediction.h"
#include "game/ProjectileMotion.h"
#include "game/World.h"
#include "network/Protocol.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
using namespace cubeshot;

void expect(bool ok, const char* what)
{
    if (!ok)
    {
        std::cerr << what << '\n';
        std::exit(1);
    }
}

Arena flat()
{
    return {{{0, 0}, {960, 540}}, {{{0, 440}, {960, 100}}}, {{100, 392}, {600, 392}}, MapId::Duel};
}

void shoot(World& w, float duration = 1)
{
    w.setPlayerInput(1, {0, false, true, {1, 0}});
    w.update(1.f / 120);
    w.clearPlayerInput(1);
    for (int i = 0; i < duration * 120; ++i)
        w.update(1.f / 120);
}

int main()
{
    expect(abilityCount == 37 && weaponCount == 17, "Expanded content catalog");
    for (auto id : playableMaps(true))
    {
        auto a = Arena::create(id, true);
        expect(!a.terrain.empty() && a.terrain.size() <= 4096, "Bounded destructible content on every map");
        expect(a.spawnPoints.size() >= 4, "Test targets supported on every map");
        for (auto spawn : a.spawnPoints)
            for (auto r : a.platforms)
                expect(!collision::overlaps({spawn, {48, 48}}, r), "Safe initial spawn");
        auto pristine = a.terrainState();
        a.damageTerrain({a.bounds.size.x / 2, a.bounds.size.y - 105}, 250, 250);
        a.restoreTerrain();
        expect(a.terrainState().health == pristine.health, "Round restoration recreates all cover and cells");
    }
    auto a = flat();
    a.addTerrain({{448, 300}, {24, 140}}, Material::Wood);
    a.rebuildTerrain();
    expect(a.terrainAt({449, 400}) != nullptr, "Terrain occupies solid cells");
    a.damageTerrain({460, 400}, 60, 200);
    expect(a.terrainAt({460, 400}) == nullptr, "Local opening is physically empty");
    for (auto r : a.platforms)
        expect(!collision::segmentHit({400, 400}, {520, 400}, r, 3), "Shots pass through opening");
    expect(a.terrainAt({460, 305}) != nullptr, "Remote material survives local hit");
    auto state = a.terrainState();
    auto clone = flat();
    clone.addTerrain({{448, 300}, {24, 140}}, Material::Wood);
    clone.rebuildTerrain();
    expect(clone.applyTerrain(state) && clone.platforms.size() == a.platforms.size(),
           "Compact state reproduces geometry");
    state.health[0] = 255;
    expect(!clone.applyTerrain(state), "Invalid material HP rejected");
    World cross(a, 2);
    cross.equip(1, WeaponKind::Crossbow);
    shoot(cross);
    expect(cross.players()[1].health < 100, "Bolts cross an existing hole");
    auto cover = flat();
    cover.addTerrain({{340, 380}, {24, 60}}, Material::Wood, true);
    cover.rebuildTerrain();
    World pierced(cover, 2);
    pierced.equip(1, WeaponKind::Crossbow);
    shoot(pierced);
    expect(pierced.players()[1].health < 100 && pierced.arena().terrain[0].health == 0,
           "Crossbow destroys and penetrates wood");
    World electric(flat(), 2);
    electric.equip(1, WeaponKind::Electric);
    shoot(electric, .1f);
    expect(electric.players()[1].health == 45, "Electric shot uses instant collision");
    World grenade(flat(), 2);
    grenade.equip(1, WeaponKind::GrenadeLauncher);
    shoot(grenade, 1.7f);
    bool exploded = false;
    for (auto e : grenade.takeEvents())
        exploded |= e.kind == EventKind::Explosion;
    expect(exploded && grenade.bullets().empty(), "Timed grenade always expires with explosion");
    Abilities build;
    build.add(AbilityKind::Power);
    build.add(AbilityKind::TwinShot);
    build.add(AbilityKind::Accuracy);
    build.add(AbilityKind::DoubleJump);
    Player player;
    player.setAbilities(build);
    player.weapon.equip(WeaponKind::Pistol);
    player.applyInput({0, false, true, {1, 0}});
    auto bullet = player.takeShot();
    expect(bullet && scatterShot(*bullet).size() == 2 && bullet->power > 1,
           "Attack modifiers survive weapon equip");
    player.position = {100, 392};
    player.update(1.f / 120, flat());
    player.requestAction(true, false, {1, 0});
    player.update(.15f, flat());
    float falling = player.velocity.y;
    player.requestAction(true, false, {1, 0});
    player.update(1.f / 120, flat());
    expect(player.airJumps == 1 && player.velocity.y < falling,
           "Extra aerial jump uses the normal jump action");
    Abilities revive;
    revive.add(AbilityKind::SecondChance);
    player = {};
    player.setAbilities(revive);
    player.burning = 2;
    player.takeDamage(150);
    expect(player.alive() && player.health == 25 && player.invulnerability > .49f && player.burning == 0,
           "Second chance prevents corpse and clears fire");
    player.takeDamage(150);
    expect(!player.alive(), "Second chance is once per round");
    auto closeArena = flat();
    closeArena.spawnPoints[1] = {260, 392};
    World shield(closeArena, 2);
    Abilities defense;
    defense.add(AbilityKind::Armor);
    shield.setAbilities(2, defense);
    shield.equip(1, WeaponKind::Shotgun);
    shoot(shield, .8f);
    expect(shield.players()[1].health == 100, "Armor blocks all pellets in a volley");
    World regen(flat(), 2);
    Abilities restoration;
    restoration.add(AbilityKind::Regeneration);
    regen.setAbilities(1, restoration);
    regen.setHealth(1, 50);
    regen.update(1);
    expect(regen.players()[0].health >= 53, "Fractional regeneration accumulates");
    {
        World combat(closeArena, 2);
        Abilities offense, defense;
        offense.add(AbilityKind::Vampirism);
        offense.add(AbilityKind::Power);
        offense.add(AbilityKind::Thorns);
        defense.add(AbilityKind::Thorns);
        defense.add(AbilityKind::ToughSkin);
        combat.setAbilities(1, offense);
        combat.setHealth(1, 50);
        combat.setAbilities(2, defense);
        shoot(combat, .8f);
        expect(combat.players()[1].health == 79 && combat.players()[0].health == 50,
               "Vampirism and thorns use real HP damage without reflected proc loops");
        World protectedWorld(closeArena, 2);
        defense.add(AbilityKind::Armor);
        protectedWorld.setAbilities(2, defense);
        protectedWorld.setAbilities(1, offense);
        protectedWorld.setHealth(1, 50);
        shoot(protectedWorld, .8f);
        expect(protectedWorld.players()[1].health == 100 && protectedWorld.players()[1].shields == 1 &&
                   protectedWorld.players()[0].health == 50,
               "Shielded damage creates neither vampirism nor reflected damage");
    }
    {
        auto metal = flat();
        metal.spawnPoints[1] = {480, 392};
        metal.addTerrain({{360, 380}, {24, 60}}, Material::Metal, true);
        metal.rebuildTerrain();
        World chain(metal, 2);
        chain.equip(1, WeaponKind::Electric);
        shoot(chain, .1f);
        expect(chain.players()[1].health == 64 && chain.arena().terrain[0].health == 0,
               "Metal relays a weakened electrical hit while the stronger direct impact destroys its cover");
        auto wood = metal;
        for (auto& c : wood.terrain)
        {
            c.material = Material::Wood;
            c.maximum = c.health = 40;
        }
        wood.rebuildTerrain();
        World stopped(wood, 2);
        stopped.equip(1, WeaponKind::Electric);
        shoot(stopped, .1f);
        expect(stopped.players()[1].health == 100 && stopped.arena().terrain[0].health == 0,
               "Electricity damages wood without conducting through it");
    }
    {
        World world(flat(), 2);
        world.equip(1, WeaponKind::GrenadeLauncher);
        auto view = observeBot(world, 1, 2);
        auto aim = botAim(view.self, *view.target, world.arena());
        expect(bool(aim), "Bot finds a safe bouncing grenade trajectory");
        world.requestAction(1, false, true, aim->direction);
        for (int i = 0; i < 220; ++i)
            world.update(1.f / 120);
        expect(world.players()[1].health < 100 && world.players()[0].alive(),
               "Actual grenade follows the bot's safe simulated shot");
    }
    {
        auto arena = flat();
        arena.addTerrain({{200, 250}, {12, 190}}, Material::Metal, true);
        arena.rebuildTerrain();
        Bullet b;
        b.weapon = WeaponKind::GrenadeLauncher;
        b.position = {180, 350};
        b.velocity = {420, 0};
        b.acceleration = 900;
        b.lifetime = 1.5f;
        bool bounced = false;
        for (int i = 0; i < 20; ++i)
        {
            advanceProjectileTerrain(b, arena, 1.f / 120);
            bounced |= b.velocity.x < 0;
        }
        expect(bounced, "Presentation and bot trajectories retain grenade bounces");
        b = {};
        b.position = {180, 350};
        b.velocity = {800, 0};
        b.ricochets = 1;
        for (int i = 0; i < 5; ++i)
            advanceProjectileTerrain(b, arena, 1.f / 120);
        expect(b.velocity.x < 0 && b.ricochets == 0, "Ordinary bullet ricochet remains visible");
    }
    {
        World world(closeArena, 2);
        world.equip(1, WeaponKind::Sniper);
        Abilities revive;
        revive.add(AbilityKind::SecondChance);
        world.setAbilities(2, revive);
        world.setPlayerInput(1, {0, false, true, {1, 0}, true});
        world.update(.6f);
        expect(world.players()[1].health == 25 && world.players()[1].secondChanceUsed &&
                   world.debris().corpses().empty(),
               "World resolves second chance before creating a corpse");
        world.clearPlayerInput(1);
        world.update(2.2f);
        world.setPlayerInput(1, {0, false, true, {1, 0}, true});
        world.update(.7f);
        expect(!world.players()[1].alive() && world.debris().corpses().size() == 1,
               "A subsequent lethal attack creates one corpse");
        world.resetPlayers();
        world.setAbilities(2, revive);
        expect(!world.players()[1].secondChanceUsed, "New round restores the passive resurrection");
    }
    {
        World world(closeArena, 2);
        Abilities small, large;
        small.add(AbilityKind::SmallBody);
        large = small;
        large.add(AbilityKind::LargeBullets);
        world.setAbilities(1, large);
        world.setAbilities(2, small);
        world.equip(1, WeaponKind::FireBoomerang);
        shoot(world, .4f);
        expect(!world.players()[1].surface.marks.empty(), "Large projectile hits a small enemy");
        for (auto& mark : world.players()[1].surface.marks)
            expect(mark.local.x >= -.01f && mark.local.x <= 48.01f && mark.local.y >= -.01f &&
                       mark.local.y <= 48.01f,
                   "Impact attaches to the real body contour");
        WorldSnapshot state;
        state.round = 1;
        state.phase = MatchPhase::Round;
        state.players = world.players();
        state.bullets = world.bullets();
        state.fires = world.fires();
        state.abilities = {large, small};
        expect(network::decodeSnapshot(network::encodeSnapshot(state)).has_value(),
               "Large-projectile wounds on small bodies remain valid on the network");
    }
    for (unsigned wins : {5u, 10u, 15u, 20u})
    {
        Match m(false, 42);
        m.connect(1);
        m.connect(2);
        expect(m.selectRules(1, {wins}, m.snapshot().selectionRevision), "Valid rule choice");
        expect(!m.selectRules(2, {5}, m.snapshot().selectionRevision), "Guest cannot change rules");
        m.setReady(1, true);
        m.setReady(2, true);
        m.update(3);
        for (unsigned round = 0; round < wins; ++round)
        {
            auto& victim = const_cast<Player&>(m.world().players()[1]);
            victim.takeDamage(100000);
            m.update(1.f / 120);
            m.update(4);
            if (round + 1 == wins)
                expect(m.phase() == MatchPhase::Finished, "Selected score finishes match exactly");
            else
            {
                expect(m.phase() == MatchPhase::Armory, "Intermission before selected goal");
                auto s = m.snapshot();
                m.chooseUpgrade(1, UpgradeKind::Continue, s.round, s.selectionRevision);
                m.chooseUpgrade(2, UpgradeKind::Continue, s.round, s.selectionRevision);
                m.update(1.f / 120);
                m.update(3);
            }
        }
        auto snapshot = m.snapshot();
        auto wire = network::encodeSnapshot(snapshot);
        auto decoded = network::decodeSnapshot(wire);
        expect(decoded && decoded->rules.wins == wins && decoded->terrain.health == snapshot.terrain.health,
               "Rules and terrain round-trip at every match length");
        wire.set_wins(19);
        expect(!network::decodeSnapshot(wire), "Invalid match goal rejected");
    }
    {
        Player small;
        small.id = 1;
        Abilities traits;
        traits.add(AbilityKind::SmallBody);
        small.setAbilities(traits);
        Debris debris;
        const Polygon tiny{{0, 0}, {2, 0}, {2, 1.5f}, {0, 1.5f}};
        expect(validPolygon(tiny), "Canonical chip is valid before body scaling");
        debris.chip(small, tiny, {1, 0}, 1, false);
        expect(debris.pieces().empty(), "Scaled subpixel chips do not become invalid physics bodies");
        debris.chip(small, {{0, 0}, {8, 0}, {8, 8}, {0, 8}}, {1, 0}, 2, false);
        expect(debris.pieces().size() == 1 && validPolygon(debris.pieces().front().outline),
               "Small characters still release visible valid fragments");
    }
    World populated(Arena::create(MapId::NatureCrossing), 2);
    for (unsigned i = 0; i < abilityCount; ++i)
    {
        Abilities one;
        expect(one.add(static_cast<AbilityKind>(i)), "Every new passive is attainable");
        populated.setAbilities(1, one);
    }
    {
        World world(Arena::create(MapId::NatureCrossing), 2);
        Abilities maximum;
        for (unsigned i = 0; i < abilityCount; ++i)
            maximum.levels[i] = abilityDefinition(static_cast<AbilityKind>(i)).maximum;
        world.setAbilities(1, maximum);
        world.setAbilities(2, maximum);
        world.equip(1, WeaponKind::Crossbow);
        world.equip(2, WeaponKind::Minigun);
        WorldSnapshot snapshot;
        snapshot.map = MapId::NatureCrossing;
        snapshot.round = 1;
        snapshot.phase = MatchPhase::Round;
        snapshot.players = world.players();
        snapshot.terrain = world.arena().terrainState();
        snapshot.abilities = {maximum, maximum};
        for (unsigned i = 0; i < 256; ++i)
        {
            Bullet b;
            b.id = i + 1;
            b.shooterId = i % 2 + 1;
            b.weapon = WeaponKind::Crossbow;
            b.velocity = {1300, 100};
            b.position = {float(200 + i), 200};
            b.lifetime = 7;
            b.age = 1;
            b.power = maximum.power();
            b.terrainPower = maximum.demolition();
            b.blastScale = maximum.blastRadius();
            b.rangeScale = maximum.range();
            b.spreadScale = maximum.accuracy();
            b.collisionRadius = 4.5f;
            b.ricochets = 1;
            b.penetrations = 2;
            snapshot.bullets.push_back(b);
        }
        for (unsigned i = 0; i < Debris::maxPieces; ++i)
        {
            PhysicsPiece piece;
            piece.id = 1000 + i;
            piece.owner = i % 2 + 1;
            piece.kind = PieceKind::Chip;
            piece.position = {float(200 + i), 400};
            piece.visualScale = .5f;
            for (unsigned n = 0; n < 12; ++n)
            {
                float a = n * 6.2831853f / 12;
                piece.outline.push_back({std::cos(a) * 10, std::sin(a) * 10});
            }
            refreshPiece(piece);
            snapshot.pieces.push_back(piece);
        }
        for (unsigned i = 0; i < maxFires; ++i)
            snapshot.fires.push_back({i + 1, i % 2 + 1, {float(i * 25 + 40), 799}, 2, 25});
        auto message = network::envelope();
        *message.mutable_snapshot() = network::encodeSnapshot(snapshot);
        expect(message.ByteSizeLong() < network::maxPacketBytes &&
                   network::decodeSnapshot(message.snapshot()).has_value(),
               "Maximum projectile, fragment, fire and ability counts fit the 64 KiB packet budget");
        std::cout << "Stress snapshot bytes: " << message.ByteSizeLong() << '\n';
    }
    std::cout << "Destruction, all match lengths, modifiers and new weapons passed\n";
}
