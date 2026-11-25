#pragma once
#include "client/Settings.h"
#include "game/Weapon.h"
#include <array>
#include <cstddef>
#include <filesystem>
#include <vector>

struct SDL_AudioStream;

namespace cubeshot::client
{
// The first weaponCount entries match WeaponKind; the remaining cues are presentation only.
enum class SoundCue : unsigned
{
    Step = weaponCount,
    Jump,
    Land,
    Reload,
    Shell,
    Ready,
    Spin,
    Overheat,
    Charge,
    Return,
    Flesh,
    Metal,
    Wood,
    Stone,
    Glass,
    Shield,
    HitConfirm,
    KillConfirm,
    Explosion,
    Break,
    Death,
    Cast,
    Heal,
    Blink,
    Parry,
    Ice,
    Mine,
    Screen,
    Boost,
    Cleanse,
    Warning,
    Fire,
    Jetpack,
    Click,
    Back,
    Countdown,
    RoundStart,
    RoundEnd,
    Roll,
    Reward,
    Count
};
inline constexpr unsigned soundCount = unsigned(SoundCue::Count);
const char* soundName(SoundCue cue);

inline SoundCue weaponSound(WeaponKind kind)
{
    return SoundCue(unsigned(kind));
}

class AudioMixer
{
public:
    static constexpr unsigned sampleRate = 48000, maxVoices = 24;
    bool load(const std::filesystem::path& directory);
    bool play(SoundCue cue, float gain = 1, float pan = 0, unsigned priority = 1, float pitch = 1);
    void mix(float* stereo, std::size_t frames);
    void clear();

    void setSettings(AudioSettings settings)
    {
        settings_ = settings;
    }

    unsigned voices() const;

    std::size_t starts() const
    {
        return starts_;
    }

    const std::vector<float>& sample(SoundCue cue) const
    {
        return bank_[unsigned(cue)];
    }

private:
    struct Voice
    {
        SoundCue cue = SoundCue::Click;
        double cursor = 0;
        float gain = 0, pan = 0, pitch = 1;
        unsigned priority = 0;
        bool active = false;
    };

    std::array<std::vector<float>, soundCount> bank_;
    std::array<Voice, maxVoices> voices_{};
    AudioSettings settings_;
    std::size_t starts_ = 0;
};

class AudioSystem
{
public:
    ~AudioSystem();
    bool initialize(const std::filesystem::path& directory);
    void pump(bool focused);
    void shutdown();
    void setSettings(AudioSettings settings);
    void clear();

    bool available() const
    {
        return stream_ != nullptr;
    }

    AudioMixer mixer;

private:
    SDL_AudioStream* stream_ = nullptr;
    bool focused_ = true;
    AudioSettings settings_;
    std::array<float, 512> buffer_{};
};
} // namespace cubeshot::client
