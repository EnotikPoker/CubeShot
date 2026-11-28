#pragma once

#include "game/Arena.h"
#include "game/Bullet.h"
#include "game/Combat.h"
#include "game/Debris.h"
#include "game/Events.h"
#include "game/Player.h"
#include "game/WorldEntity.h"

#include <array>
#include <random>
#include <vector>

namespace cubeshot
{

struct TestEdit;
struct TestConfiguration;

class World
{
public:
    static constexpr PlayerId localPlayerId = 1;

    // Explicit player count decouples the game mode from available spawn points.
    // Zero retains support for custom test arenas using every spawn.
    explicit World(Arena arena = Arena::createDefault(), std::size_t playerCount = 0);

    void setTeams(const std::array<TeamId, maxPlayers>& teams, bool friendlyFire);
    bool allied(PlayerId a, PlayerId b) const;
    bool canHarm(PlayerId source, PlayerId target) const;
    void arrangeSpawns(const std::array<TeamId, maxPlayers>& teams, bool teamMode, unsigned rotation);
    void setPlayerInput(PlayerId id, const PlayerInput& input);
    void clearPlayerInput(PlayerId id);
    void requestAction(PlayerId id, bool jump, bool shoot, Vec2 aim, std::uint64_t sequence = 0,
                       bool reload = false, unsigned activeMask = 0);
    void setActives(PlayerId id, const ActiveLoadout& loadout);

    const std::vector<WorldEntity>& entities() const
    {
        return entities_;
    }

    void setModifiers(SimulationModifiers modifiers);
    void respawnPlayer(PlayerId id, Vec2 position, WeaponKind weapon, const Abilities& abilities,
                       const ActiveLoadout& active, const ActiveSlots& previous, unsigned life);
    void advanceDeadCooldowns(float dt);

    void limitContinuousDebris(float dt)
    {
        debris_.limitContinuous(dt);
    }

    void resetPlayers(bool swapSpawns = false);
    void equip(PlayerId id, WeaponKind kind);
    bool editTest(const TestEdit& edit, const TestConfiguration& config);
    void setHealth(PlayerId id, unsigned percent);
    void setAbilities(PlayerId id, const Abilities& abilities);

    void setVisualSeed(unsigned seed)
    {
        visualRandom_.seed(seed);
    }

    void update(float deltaSeconds, bool damageLiving = true);

    std::vector<GameEvent> takeEvents();
    const Arena& arena() const;
    const std::vector<Player>& players() const;
    const std::vector<Bullet>& bullets() const;
    const Debris& debris() const;

    const std::vector<FirePatch>& fires() const
    {
        return fires_;
    }

private:
    std::array<TeamId, maxPlayers> teams_{};
    bool friendlyFire_ = false;
    void resetEntities();
    void updateEntities(float dt);
    void syncScreens();
    std::optional<std::pair<float, std::uint64_t>> traceEntities(Vec2 from, Vec2 to, float radius,
                                                                 const Bullet& attack) const;
    void hitEntity(std::uint64_t id, const Bullet& attack, Vec2 at);
    void beam(const Bullet& attack);
    void removeOwnedEntities(PlayerId id);
    std::vector<WorldEntity> entities_;
    std::uint64_t nextEntityId_ = 1;
    float hazardTick_ = 0;

    struct EchoAttack
    {
        Bullet attack;
        float delay = .12f;
    };

    std::vector<EchoAttack> echoes_;
    std::array<unsigned, 4> testActiveRequests_{};
    void castActive(Player& player, unsigned slot);
    void radialActive(Player& player, ActiveKind kind);
    void updateBleeding(float dt);
    bool damage(Player& player, const Bullet& bullet, Vec2 at, int amount, bool ignite = false,
                DamageContext context = {});
    void explode(const Bullet& bullet, Vec2 at, PlayerId direct = 0);
    void igniteSurface(Vec2 at, PlayerId owner);
    void updateFire(float dt);
    void melee(const Bullet& attack);
    void electric(const Bullet& attack);
    std::vector<FirePatch> fires_;
    std::uint64_t nextFireId_ = 1;
    bool moveBullet(Bullet& bullet, Vec2 destination);
    void updateBullets(float deltaSeconds);

    void emit(EventKind kind, const Bullet& bullet, Vec2 position, PlayerId target = 0);
    std::mt19937 visualRandom_{std::random_device{}()};
    Arena arena_;
    Debris debris_;
    bool damageLiving_ = true;
    std::vector<Vec2> pendingImpact_;
    std::size_t playerCount_ = 0;
    std::uint64_t nextEvent_ = 1, tick_ = 0;
    std::vector<GameEvent> events_;
    std::vector<Player> players_;
    std::vector<Bullet> bullets_;
    BulletId nextBulletId_ = 1;
    std::vector<int> pendingDamage_;
    std::vector<std::vector<Bullet>> pendingCauses_;
    std::vector<std::vector<ImpactContext>> pendingContacts_;
};

} // namespace cubeshot
