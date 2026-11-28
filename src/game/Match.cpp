#include "game/Match.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace cubeshot
{
namespace
{
bool validId(PlayerId id)
{
    return id >= 1 && id <= maxPlayers;
}
} // namespace

void Match::rebuildWorld()
{
    world_ = World{Arena::create(world_.arena().map), rules_.participants};
    world_.setTeams(rules_.format == MatchFormat::Teams ? teams_ : std::array<TeamId, maxPlayers>{},
                    rules_.friendlyFire);
}

bool Match::configureSlot(PlayerId actor, PlayerId slot, SlotKind kind, unsigned difficulty, TeamId team,
                          std::uint64_t revision)
{
    if (actor != host_ || !validId(slot) || slot > rules_.participants || phase_ != MatchPhase::Lobby ||
        revision != selectionRevision_ || unsigned(kind) > 2 || difficulty > 2 || team < 1 || team > 2 ||
        slots_[slot - 1] == SlotKind::Human || kind == SlotKind::Human)
        return false;
    slots_[slot - 1] = kind;
    connected_[slot - 1] = kind != SlotKind::Empty;
    botDifficulty_[slot - 1] = difficulty;
    teams_[slot - 1] = team;
    ready_ = {};
    ++selectionRevision_;
    return true;
}

bool Match::selectTeam(PlayerId actor, TeamId team, std::uint64_t revision)
{
    if (!validId(actor) || actor > rules_.participants || slots_[actor - 1] != SlotKind::Human || team < 1 ||
        team > 2 || phase_ != MatchPhase::Lobby || revision != selectionRevision_)
        return false;
    teams_[actor - 1] = team;
    ready_ = {};
    ++selectionRevision_;
    return true;
}

void Match::replaceWithBot(PlayerId id)
{
    if (!validId(id) || id > rules_.participants)
        return;
    slots_[id - 1] = SlotKind::Bot;
    botDifficulty_[id - 1] = 1;
    connected_[id - 1] = true;
    ready_[id - 1] = true;
    clearInput(id);
    lastAction_[id - 1] = 0;
    ++selectionRevision_;
}

bool Match::selectRules(PlayerId id, MatchRules rules, std::uint64_t revision)
{
    if (id != host_ || !connected_[id - 1] || phase_ != MatchPhase::Lobby || revision != selectionRevision_ ||
        !rules.valid())
        return false;
    for (unsigned i = rules.participants; i < maxPlayers; ++i)
        if (slots_[i] == SlotKind::Human)
            return false;
    rules_ = rules;
    for (unsigned i = rules_.participants; i < maxPlayers; ++i)
    {
        slots_[i] = SlotKind::Empty;
        connected_[i] = false;
    }
    rebuildWorld();
    mode_ = {};
    ready_ = {};
    ++selectionRevision_;
    return true;
}

bool Match::selectMap(PlayerId id, MapId map, std::uint64_t revision)
{
    if (!validId(id) || id > rules_.participants || !connected_[id - 1] || phase_ != MatchPhase::Lobby ||
        revision != selectionRevision_ || !validMap(map))
        return false;
    if (!voting_ && id != host_)
        return false;
    if (voting_)
        votes_[id - 1] = static_cast<int>(map);
    else
        world_ = World{Arena::create(map), rules_.participants};
    world_.setTeams(rules_.format == MatchFormat::Teams ? teams_ : std::array<TeamId, maxPlayers>{},
                    rules_.friendlyFire);
    ready_ = {};
    ++selectionRevision_;
    return true;
}

bool Match::changeMap(PlayerId id, std::uint64_t revision)
{
    if (!validId(id) || id > rules_.participants || !connected_[id - 1] || phase_ != MatchPhase::Finished ||
        revision != selectionRevision_ || (!voting_ && id != host_))
        return false;
    phase_ = MatchPhase::Lobby;
    mode_ = {};
    score_ = {};
    teamScore_ = {};
    winnerTeam_ = 0;
    ready_ = {};
    votes_ = {{-1, -1, -1, -1}};
    rematch_ = false;
    roundsPlayed_ = 0;
    loadout_ = {};
    abilities_ = {};
    activeLoadout_ = {};
    rolledActive_ = {};
    upgrade_ = {};
    rolledAbility_ = {AbilityKind::None, AbilityKind::None};
    weaponChoice_ = {};
    spinRemaining_ = {};
    winner_ = 0;
    ++round_;
    ++selectionRevision_;
    world_.resetPlayers();
    events_.clear();
    return true;
}

std::vector<GameEvent> Match::takeEvents()
{
    auto out = std::move(events_);
    events_.clear();
    return out;
}

void Match::connect(PlayerId id)
{
    if (!validId(id))
        return;
    if (id > rules_.participants || (phase_ != MatchPhase::Lobby && phase_ != MatchPhase::Finished))
        return;
    slots_[id - 1] = SlotKind::Human;
    connected_[id - 1] = true;
    ready_[id - 1] = false;
}

void Match::disconnect(PlayerId id, AbortReason reason)
{
    if (!validId(id) || id > rules_.participants || !connected_[id - 1])
        return;
    if (phase_ == MatchPhase::Round || phase_ == MatchPhase::RoundEnd || phase_ == MatchPhase::Armory)
    {
        replaceWithBot(id);
        return;
    }
    slots_[id - 1] = SlotKind::Empty;
    connected_[id - 1] = false;
    ready_ = {};
    score_ = {};
    teamScore_ = {};
    winnerTeam_ = 0;
    lastAction_ = {};
    aborted_ = reason;
    phase_ = MatchPhase::Lobby;
    mode_ = {};
    remaining_ = 0;
    winner_ = 0;
    roundsPlayed_ = 0;
    loadout_ = {};
    abilities_ = {};
    activeLoadout_ = {};
    rolledActive_ = {};
    upgrade_ = {};
    rolledAbility_ = {AbilityKind::None, AbilityKind::None};
    weaponChoice_ = {};
    spinRemaining_ = {};
    votes_ = {{-1, -1, -1, -1}};
    rematch_ = false;
    events_.clear();
    ++selectionRevision_;
    ++round_; // Любые пакеты прерванного раунда больше не действуют.
    world_.resetPlayers();
}

void Match::beginCountdown()
{
    if (phase_ == MatchPhase::Lobby || phase_ == MatchPhase::Finished)
    {
        mode_ = {};
        weaponChoice_ = {WeaponChoice::Ready, WeaponChoice::Ready};
        loadout_ = {};
        abilities_ = {};
        activeLoadout_ = {};
        rolledActive_ = {};
    }
    upgrade_ = {};
    rolledAbility_ = {AbilityKind::None, AbilityKind::None};
    weaponChoice_ = {};
    spinRemaining_ = {};
    if (phase_ == MatchPhase::Finished)
    {
        score_ = {};
        teamScore_ = {};
        winnerTeam_ = 0;
        teamScore_ = {};
        roundsPlayed_ = 0;
    }
    if (voting_ && rules_.participants == 2 && phase_ == MatchPhase::Lobby && !rematch_)
    {
        static std::mt19937 rng(std::random_device{}());
        const auto pick =
            votes_[0] == votes_[1] ? votes_[0] : votes_[std::uniform_int_distribution<int>(0, 1)(rng)];
        world_ = World{Arena::create(static_cast<MapId>(pick)), rules_.participants};
    }
    rematch_ = true;
    ++selectionRevision_;
    ++round_;
    lastAction_ = {};
    aborted_ = AbortReason::None;
    winner_ = 0;
    world_.setVisualSeed(visualSeed_ ^ round_ * 2654435761u);
    world_.resetPlayers(roundsPlayed_ % 2 != 0);
    world_.setTeams(rules_.format == MatchFormat::Teams ? teams_ : std::array<TeamId, maxPlayers>{},
                    rules_.friendlyFire);
    if (rules_.participants > 2)
        world_.arrangeSpawns(teams_, rules_.format == MatchFormat::Teams, roundsPlayed_);
    weaponChoice_.fill(WeaponChoice::Ready);
    winnerTeam_ = 0;
    if (rules_.mode == GameMode::ArmsRace)
    {
        auto chain = armsChain(rules_.stages);
        for (unsigned i = 0; i < rules_.participants; ++i)
            loadout_[i] =
                chain[std::min(rules_.format == MatchFormat::Teams ? teamScore_[teams_[i] - 1] : score_[i],
                               unsigned(chain.size() - 1))];
    }
    if (rules_.mode == GameMode::Chaos)
    {
        if (mode_.nextChaos == ChaosKind::None)
            prepareChaos();
        mode_.chaos = mode_.nextChaos;
    }
    world_.setModifiers(chaosModifiers(mode_.chaos));
    for (unsigned i = 0; i < rules_.participants; ++i)
    {
        world_.equip(i + 1, loadout_[i]);
        world_.setAbilities(i + 1, abilities_[i]);
        world_.setActives(i + 1, activeLoadout_[i]);
    }
    phase_ = MatchPhase::Countdown;
    remaining_ = 3;
}

void Match::setReady(PlayerId id, bool ready)
{
    if (!validId(id) || id > rules_.participants || !connected_[id - 1] ||
        (phase_ != MatchPhase::Lobby && phase_ != MatchPhase::Finished))
        return;
    if (ready && voting_ && rules_.participants == 2 && phase_ == MatchPhase::Lobby &&
        (votes_[0] < 0 || votes_[1] < 0))
        return;
    ready_[id - 1] = ready;
    bool all = true;
    unsigned counts[2]{};
    for (unsigned i = 0; i < rules_.participants; ++i)
    {
        all = all && connected_[i] && (ready_[i] || slots_[i] == SlotKind::Bot);
        if (teams_[i] >= 1 && teams_[i] <= 2)
            ++counts[teams_[i] - 1];
    }
    if (all && (rules_.format != MatchFormat::Teams || (counts[0] == 2 && counts[1] == 2)))
        beginCountdown();
}

void Match::setInput(PlayerId id, const PlayerInput& input)
{
    if (!validId(id) || id > rules_.participants || !connected_[id - 1] ||
        (phase_ != MatchPhase::Round && phase_ != MatchPhase::RoundEnd))
        return;
    if (!std::isfinite(input.moveDirection) || !std::isfinite(input.aimDirection.x) ||
        !std::isfinite(input.aimDirection.y))
        return;
    if (input.lifeRevision != ~0u && input.lifeRevision != world_.players()[id - 1].lifeRevision)
        return;
    PlayerInput continuous = input;
    continuous.triggerHeld = input.triggerHeld || input.shoot;
    continuous.jump = continuous.shoot = continuous.reload = continuous.activeQ = continuous.activeE = false;
    world_.setPlayerInput(id, continuous);
}

void Match::clearInput(PlayerId id)
{
    if (validId(id))
        world_.clearPlayerInput(id);
}

bool Match::action(PlayerId id, std::uint32_t round, std::uint64_t sequence, bool jump, bool shoot, Vec2 aim,
                   bool reload, unsigned activeMask, unsigned life)
{
    if (validId(id) && id <= rules_.participants && life != ~0u &&
        world_.players()[id - 1].lifeRevision != life)
        return false;
    if (activeMask > 3 || !validId(id) || id > rules_.participants || !connected_[id - 1] ||
        (phase_ != MatchPhase::Round && phase_ != MatchPhase::RoundEnd) || round != round_ || sequence == 0 ||
        sequence <= lastAction_[id - 1] || !std::isfinite(aim.x) || !std::isfinite(aim.y) ||
        std::hypot(aim.x, aim.y) < 0.0001f || !world_.players()[id - 1].alive())
        return false;
    lastAction_[id - 1] = sequence;
    world_.requestAction(id, jump, shoot, aim, sequence, reload, activeMask);
    return true;
}

bool Match::chooseWeapon(PlayerId id, bool reroll, std::uint32_t round, std::uint64_t revision)
{
    return chooseUpgrade(id, reroll ? UpgradeKind::Weapon : UpgradeKind::Continue, round, revision);
}

bool Match::chooseUpgrade(PlayerId id, UpgradeKind kind, std::uint32_t round, std::uint64_t revision,
                          unsigned life)
{
    bool personal = rules_.mode == GameMode::Control && phase_ == MatchPhase::Round && validId(id) &&
                    id <= rules_.participants && mode_.respawn[id - 1].choosing;
    if (personal && life != ~0u && life != mode_.respawn[id - 1].life)
        return false;
    if (rules_.mode == GameMode::ArmsRace && kind == UpgradeKind::Weapon)
        return false;
    if (!validId(id) || id > rules_.participants || !connected_[id - 1] ||
        (!personal && phase_ != MatchPhase::Armory) || round != round_ || revision != selectionRevision_ ||
        weaponChoice_[id - 1] != WeaponChoice::Pending || static_cast<unsigned>(kind) > 4)
        return false;
    const auto slot = id - 1;
    if (kind == UpgradeKind::Ability)
    {
        const auto pool = abilities_[slot].available(loadout_[slot], activeLoadout_[slot]);
        if (pool.empty())
            return false;
        rolledAbility_[slot] =
            pool[std::uniform_int_distribution<unsigned>(0, static_cast<unsigned>(pool.size() - 1))(random_)];
    }
    else if (kind == UpgradeKind::ActiveQ || kind == UpgradeKind::ActiveE)
    {
        unsigned index = kind == UpgradeKind::ActiveQ ? 0 : 1;
        std::vector<ActiveKind> pool;
        for (unsigned k = 1; k <= activeCount; ++k)
            if (ActiveKind(k) != activeLoadout_[slot][1 - index])
                pool.push_back(ActiveKind(k));
        rolledActive_[slot] =
            pool[std::uniform_int_distribution<unsigned>(0, unsigned(pool.size() - 1))(random_)];
    }
    else if (kind == UpgradeKind::Weapon)
        rolled_[slot] =
            static_cast<WeaponKind>(std::uniform_int_distribution<unsigned>(0, weaponCount - 1)(random_));
    upgrade_[slot] = kind;
    if (kind == UpgradeKind::Continue)
        weaponChoice_[slot] = WeaponChoice::Ready;
    else
    {
        spinRemaining_[slot] = wheelSeconds;
        weaponChoice_[slot] = WeaponChoice::Spinning;
    }
    return true;
}

void Match::collectEvents()
{
    for (auto event : world_.takeEvents())
    {
        event.tick = tick_;
        event.round = round_;
        events_.push_back(event);
    }
    if (events_.size() > 256)
        events_.erase(events_.begin(), events_.end() - 256);
}

void Match::update(float dt)
{
    if (!std::isfinite(dt) || dt <= 0)
        return;
    ++tick_;
    if (phase_ == MatchPhase::Armory)
    {
        updateChoices(dt);
        decisionRemaining_ = std::max(0.f, decisionRemaining_ - dt);
        bool all = true;
        for (unsigned i = 0; i < rules_.participants; ++i)
        {
            if (decisionRemaining_ == 0 && weaponChoice_[i] == WeaponChoice::Pending)
                weaponChoice_[i] = WeaponChoice::Ready;
            all = all && weaponChoice_[i] == WeaponChoice::Ready;
        }
        if (all)
            beginCountdown();
        return;
    }
    if (phase_ == MatchPhase::Countdown || phase_ == MatchPhase::RoundEnd)
    {
        if (phase_ == MatchPhase::RoundEnd)
        {
            world_.update(dt, false);
            collectEvents();
        }
        remaining_ = std::max(0.f, remaining_ - dt);
        if (remaining_ <= .0001f)
        {
            remaining_ = 0;
            if (phase_ == MatchPhase::RoundEnd)
            {
                const bool finished = (winner_ || winnerTeam_) &&
                                      (winnerTeam_ ? teamScore_[winnerTeam_ - 1] : score_[winner_ - 1]) >=
                                          (rules_.mode == GameMode::ArmsRace ? rules_.stages : rules_.wins);
                phase_ = finished ? MatchPhase::Finished : MatchPhase::Armory;
                decisionRemaining_ = 30;
                weaponChoice_ = {};
                spinRemaining_ = {};
                rolled_ = loadout_;
                upgrade_ = {};
                rolledAbility_ = {AbilityKind::None, AbilityKind::None};
                if (rules_.mode == GameMode::Chaos)
                    prepareChaos();
                ++selectionRevision_;
                for (PlayerId id = 1; id <= rules_.participants; ++id)
                    world_.clearPlayerInput(id);
            }
            else
            {
                phase_ = MatchPhase::Round;
                ready_ = {};
            }
        }
    }
    else if (phase_ == MatchPhase::Round)
    {
        if (rules_.mode == GameMode::Control)
        {
            updateControl(dt);
            return;
        }
        world_.update(dt);
        collectEvents();
        std::array<bool, maxPlayers> aliveSides{};
        for (unsigned i = 0; i < rules_.participants; ++i)
            if (world_.players()[i].alive())
                aliveSides[side(i)] = true;
        const auto alive = std::count(aliveSides.begin(), aliveSides.end(), true);
        if (alive <= 1)
        {
            winner_ = 0;
            winnerTeam_ = 0;
            if (alive == 1)
            {
                unsigned winning =
                    unsigned(std::find(aliveSides.begin(), aliveSides.end(), true) - aliveSides.begin());
                if (rules_.format == MatchFormat::Teams)
                {
                    winnerTeam_ = winning + 1;
                    ++teamScore_[winning];
                }
                else
                {
                    winner_ = winning + 1;
                    ++score_[winning];
                }
            }
            ++roundsPlayed_;
            for (PlayerId id = 1; id <= rules_.participants; ++id)
                world_.clearPlayerInput(id);
            phase_ = MatchPhase::RoundEnd;
            remaining_ = aftermathSeconds;
            ready_ = {};
        }
    }
}

WorldSnapshot Match::snapshot() const
{
    WorldSnapshot s{tick_,  round_,  phase_,   remaining_,       score_,          connected_,
                    ready_, winner_, aborted_, world_.players(), world_.bullets()};
    s.slots = slots_;
    s.teams = teams_;
    s.botDifficulty = botDifficulty_;
    s.teamScore = teamScore_;
    s.winnerTeam = winnerTeam_;
    s.host = host_;
    s.decisionRemaining = decisionRemaining_;
    s.mode = mode_;
    s.entities = world_.entities();
    s.activeLoadout = activeLoadout_;
    s.rolledActive = rolledActive_;
    s.rules = rules_;
    s.terrain = world_.arena().terrainState();
    s.fires = world_.fires();
    s.corpses = world_.debris().corpses();
    s.pieces = world_.debris().pieces();
    s.abilities = abilities_;
    s.upgrade = upgrade_;
    s.rolledAbility = rolledAbility_;
    s.weaponChoice = weaponChoice_;
    s.loadout = loadout_;
    s.rolled = rolled_;
    s.spinRemaining = spinRemaining_;
    s.map = world_.arena().map;
    s.voting = voting_;
    s.votes = votes_;
    s.selectionRevision = selectionRevision_;
    return s;
}
} // namespace cubeshot

namespace cubeshot
{
void Match::updateChoices(float dt)
{
    for (unsigned i = 0; i < rules_.participants; ++i)
        if (weaponChoice_[i] == WeaponChoice::Spinning || weaponChoice_[i] == WeaponChoice::Revealing)
        {
            spinRemaining_[i] = std::max(0.f, spinRemaining_[i] - dt);
            if (spinRemaining_[i] <= .0001f)
            {
                if (weaponChoice_[i] == WeaponChoice::Spinning)
                {
                    if (upgrade_[i] == UpgradeKind::Ability)
                        abilities_[i].add(rolledAbility_[i]);
                    else if (upgrade_[i] == UpgradeKind::ActiveQ || upgrade_[i] == UpgradeKind::ActiveE)
                        activeLoadout_[i][upgrade_[i] == UpgradeKind::ActiveQ ? 0 : 1] = rolledActive_[i];
                    else
                        loadout_[i] = rolled_[i];
                    weaponChoice_[i] = WeaponChoice::Revealing;
                    spinRemaining_[i] =
                        (upgrade_[i] != UpgradeKind::Weapon || rules_.mode == GameMode::Control)
                            ? abilityReviewSeconds
                            : spinSeconds - wheelSeconds;
                }
                else
                {
                    spinRemaining_[i] = 0;
                    weaponChoice_[i] = WeaponChoice::Ready;
                }
            }
        }
}
} // namespace cubeshot
