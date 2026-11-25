#include "client/Audio.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace cubeshot::client
{
const char* soundName(SoundCue cue)
{
    static constexpr const char* names[]{
        "pistol",   "shotgun",   "automatic",   "smg",          "revolver",
        "rifle",    "sniper",    "minigun",     "rpg",          "flamethrower",
        "bat",      "katana",    "knife",       "boomerang",    "grenade_launcher",
        "crossbow", "electric",  "beam",        "disc",         "hammer",
        "sticky",   "step",      "jump",        "land",         "reload",
        "shell",    "ready",     "spin",        "overheat",     "charge",
        "return",   "flesh",     "metal",       "wood",         "stone",
        "glass",    "shield",    "hit_confirm", "kill_confirm", "explosion",
        "break",    "death",     "cast",        "heal",         "blink",
        "parry",    "ice",       "mine",        "screen",       "boost",
        "cleanse",  "warning",   "fire",        "jetpack",      "click",
        "back",     "countdown", "round_start", "round_end",    "roll",
        "reward"};
    static_assert(std::size(names) == soundCount);
    return unsigned(cue) < soundCount ? names[unsigned(cue)] : "";
}

bool AudioMixer::load(const std::filesystem::path& directory)
{
    clear();
    for (unsigned i = 0; i < soundCount; ++i)
    {
        SDL_AudioSpec spec{};
        Uint8* data = nullptr;
        Uint32 length = 0;
        const auto file = directory / (std::string(soundName(SoundCue(i))) + ".wav");
        if (!SDL_LoadWAV(file.u8string().c_str(), &spec, &data, &length))
        {
            SDL_Log("Audio sample unavailable: %s: %s", file.u8string().c_str(), SDL_GetError());
            return false;
        }
        const bool valid = spec.freq == int(sampleRate) && spec.channels == 1 &&
                           spec.format == SDL_AUDIO_S16LE && length >= 2 && length <= sampleRate * 4;
        if (valid)
        {
            auto& sample = bank_[i];
            sample.resize(length / 2);
            for (std::size_t n = 0; n < sample.size(); ++n)
            {
                const auto value = Uint16(data[2 * n]) | (Uint16(data[2 * n + 1]) << 8);
                sample[n] = float(static_cast<Sint16>(value)) / 32768;
            }
        }
        SDL_free(data);
        if (!valid)
            return false;
    }
    return true;
}

bool AudioMixer::play(SoundCue cue, float gain, float pan, unsigned priority, float pitch)
{
    if (unsigned(cue) >= soundCount || bank_[unsigned(cue)].empty() || !std::isfinite(gain) ||
        !std::isfinite(pan) || !std::isfinite(pitch) || gain <= 0 || settings_.master == 0 ||
        (unsigned(cue) >= unsigned(SoundCue::Click) ? settings_.interface : settings_.combat) == 0)
        return false;
    // Dense automatic fire cannot occupy every voice or evict important feedback.
    unsigned same = 0;
    for (const auto& voice : voices_)
        same += voice.active && voice.cue == cue;
    if (same >= (priority >= 3 ? 4u : 3u))
        return false;
    auto slot = std::find_if(voices_.begin(), voices_.end(), [](const auto& v) { return !v.active; });
    if (slot == voices_.end())
    {
        slot = std::min_element(
            voices_.begin(), voices_.end(), [](const auto& a, const auto& b)
            { return a.priority < b.priority || (a.priority == b.priority && a.cursor > b.cursor); });
        if (slot->priority > priority)
            return false;
    }
    *slot = {
        cue,      0,   std::clamp(gain, 0.f, 2.f), std::clamp(pan, -1.f, 1.f), std::clamp(pitch, .75f, 1.4f),
        priority, true};
    ++starts_;
    return true;
}

void AudioMixer::mix(float* out, std::size_t frames)
{
    std::fill(out, out + frames * 2, 0.f);
    for (auto& v : voices_)
    {
        if (!v.active)
            continue;
        const auto& sample = bank_[unsigned(v.cue)];
        const float category =
            float(unsigned(v.cue) >= unsigned(SoundCue::Click) ? settings_.interface : settings_.combat) /
            100;
        const float gain = v.gain * settings_.master / 100.f * category * .65f;
        const float left = std::sqrt((1 - v.pan) * .5f) * gain, right = std::sqrt((1 + v.pan) * .5f) * gain;
        for (std::size_t i = 0; i < frames; ++i)
        {
            const auto n = std::size_t(v.cursor);
            if (n + 1 >= sample.size())
            {
                v.active = false;
                break;
            }
            const float t = float(v.cursor - n);
            const float value = sample[n] + (sample[n + 1] - sample[n]) * t;
            out[2 * i] += value * left;
            out[2 * i + 1] += value * right;
            v.cursor += v.pitch;
        }
    }
    for (std::size_t i = 0; i < frames * 2; ++i)
        out[i] = out[i] / (1 + std::abs(out[i])); // bounded soft limiter, no clipping at dense volleys
}

void AudioMixer::clear()
{
    voices_ = {};
}

unsigned AudioMixer::voices() const
{
    return unsigned(std::count_if(voices_.begin(), voices_.end(), [](auto v) { return v.active; }));
}

AudioSystem::~AudioSystem()
{
    shutdown();
}

void AudioSystem::shutdown()
{
    if (stream_)
        SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
    mixer.clear();
}

void AudioSystem::setSettings(AudioSettings settings)
{
    if (!(settings == settings_) && stream_)
        SDL_ClearAudioStream(stream_);
    settings_ = settings;
    mixer.setSettings(settings);
}

bool AudioSystem::initialize(const std::filesystem::path& directory)
{
    shutdown();
    if (!mixer.load(directory))
        return false;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        SDL_Log("Audio disabled: %s", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec format{SDL_AUDIO_F32, 2, int(AudioMixer::sampleRate)};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &format, nullptr, nullptr);
    if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_))
    {
        SDL_Log("Audio playback unavailable: %s", SDL_GetError());
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        return false;
    }
    return true;
}

void AudioSystem::clear()
{
    mixer.clear();
    if (stream_)
        SDL_ClearAudioStream(stream_);
}

void AudioSystem::pump(bool focused)
{
    if (focused != focused_)
    {
        clear();
        focused_ = focused;
    }
    if (!stream_ || !focused)
    {
        mixer.clear();
        return;
    }
    int queued = SDL_GetAudioStreamQueued(stream_);
    if (queued < 0)
    {
        clear();
        return;
    }
    // Keep at most ~32 ms queued. A long frame never creates a delayed audio backlog.
    constexpr int target = 1536 * 2 * int(sizeof(float));
    for (unsigned blocks = 0; queued < target && blocks < 6; ++blocks)
    {
        mixer.mix(buffer_.data(), buffer_.size() / 2);
        if (!SDL_PutAudioStreamData(stream_, buffer_.data(), int(sizeof(buffer_))))
        {
            clear();
            break;
        }
        queued += sizeof(buffer_);
    }
}
} // namespace cubeshot::client
