#pragma once
#include "client/Audio.h"
#include "game/Match.h"
#include <map>
#include <set>
#include <tuple>

namespace cubeshot::client
{
// Main-thread presentation: never changes simulation or server state.
class SoundScene
{
public:
    explicit SoundScene(AudioMixer& mixer) : mixer_(mixer) {}

    void clear();
    void update(float dt, const std::vector<Player>& players, const Arena& arena,
                const std::vector<GameEvent>& events, PlayerId own, const std::vector<WorldEntity>& entities,
                const std::vector<Bullet>& bullets);
    void match(MatchPhase phase, unsigned round, float remaining, const std::array<WeaponChoice, 2>& choices,
               PlayerId own);

private:
    void at(SoundCue cue, Vec2 position, const Arena& arena, float gain = 1, unsigned priority = 1);
    bool fresh(const GameEvent& event, PlayerId own);

    struct Body
    {
        Vec2 position{};
        unsigned life = 0;
        bool grounded = false, alive = false, reload = false, hot = false, charge = false;
        int ammo = 0;
        WeaponKind weapon = WeaponKind::Pistol;
        float step = 0, motor = 0, jet = 0;
    };

    AudioMixer& mixer_;
    float time_ = 0, confirmAt_ = -1, impactAt_ = -1, blastAt_ = -1, rollAt_ = -1;
    unsigned budget_ = 0;
    std::map<PlayerId, Body> bodies_;
    std::map<std::pair<bool, std::uint64_t>, float> warnings_;
    std::map<BulletId, bool> boomerangs_;
    std::set<std::tuple<unsigned, std::uint64_t>> seen_;
    using ShotKey = std::tuple<unsigned, PlayerId, unsigned, std::uint64_t>;
    std::map<ShotKey, float> shots_;
    using CastKey = std::tuple<unsigned, PlayerId, unsigned, std::uint64_t, ActiveKind, bool>;
    std::map<CastKey, float> casts_;
    unsigned round_ = 0;
    int countdown_ = -1;
    MatchPhase phase_ = MatchPhase::Lobby;
    WeaponChoice choice_ = WeaponChoice::Ready;
};
} // namespace cubeshot::client
