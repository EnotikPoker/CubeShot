#include "client/Audio.h"
#include "client/SoundScene.h"
#include "game/World.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace cubeshot;
using namespace cubeshot::client;

namespace
{
void expect(bool value, const char* why)
{
    if (!value)
        throw std::runtime_error(why);
}

std::filesystem::path bank()
{
    return std::filesystem::u8path(SDL_GetBasePath()) / "assets" / "audio";
}

void finish(AudioMixer& mixer)
{
    std::array<float, 512> output{};
    for (int n = 0; n < 400; ++n)
        mixer.mix(output.data(), 256);
}

void samplesAndMix()
{
    AudioMixer mixer;
    expect(mixer.load(bank()), "All sound assets load from the executable directory");
    std::set<std::pair<std::size_t, long>> identities;
    for (unsigned n = 0; n < soundCount; ++n)
    {
        const auto& sample = mixer.sample(SoundCue(n));
        expect(!sample.empty() && sample.size() <= 96000, "Audio bank bounded and complete");
        double energy = 0;
        for (float s : sample)
        {
            expect(std::isfinite(s) && std::abs(s) <= 1, "Finite PCM data");
            energy += s * s;
        }
        expect(energy > 1, "Every cue is audible rather than an empty placeholder");
        if (n < weaponCount)
            identities.emplace(sample.size(), long(energy * 1000));
    }
    expect(identities.size() == weaponCount, "All weapons have distinct sound samples");
    std::array<float, 1024> output{};
    mixer.setSettings({100, 100, 100});
    mixer.play(weaponSound(WeaponKind::Pistol), 1, -1);
    mixer.mix(output.data(), 512);
    float left = 0, right = 0;
    for (int i = 0; i < 512; ++i)
    {
        left += std::abs(output[2 * i]);
        right += std::abs(output[2 * i + 1]);
    }
    expect(left > 1 && right == 0, "Stereo panning follows sound position");
    mixer.clear();
    mixer.setSettings({100, 0, 100});
    expect(!mixer.play(SoundCue::HitConfirm) && mixer.play(SoundCue::Click),
           "Category mute preserves interface");
    mixer.setSettings({0, 100, 100});
    mixer.mix(output.data(), 512);
    expect(std::all_of(output.begin(), output.end(), [](float x) { return x == 0; }),
           "Master mute affects existing voices");
    mixer.clear();
    mixer.setSettings({100, 100, 100});
    for (unsigned n = 0; n < 500; ++n)
        mixer.play(SoundCue(n % soundCount), 2, 0, 1);
    expect(mixer.voices() <= AudioMixer::maxVoices && mixer.play(SoundCue::KillConfirm, 1, 0, 3),
           "Dense combat remains bounded and important feedback gets a voice");
    auto begin = std::chrono::steady_clock::now();
    for (int n = 0; n < 200; ++n)
    {
        mixer.play(weaponSound(WeaponKind(n % weaponCount)), 1, 0, 1);
        mixer.mix(output.data(), 512);
        for (float x : output)
            expect(std::isfinite(x) && std::abs(x) < 1, "Dense mix stays finite without clipping");
    }
    const auto elapsed =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    std::cout << "Mixer: 200 blocks / 102400 frames in " << elapsed << " ms\n";
    finish(mixer);
    expect(mixer.voices() == 0, "Finished voices are released");
}

void eventDedupAndMovement()
{
    AudioMixer mixer;
    expect(mixer.load(bank()), "Load event fixture bank");
    SoundScene scene(mixer);
    World world(Arena::createDuel(), 2);
    auto players = world.players();
    auto arena = world.arena();
    for (auto& p : players)
        p.grounded = true;
    auto update = [&](std::vector<GameEvent> events = {})
    { scene.update(.01f, players, arena, events, 1, {}, {}); };
    update();
    GameEvent shot;
    shot.round = 1;
    shot.kind = EventKind::Shot;
    shot.actor = 1;
    shot.volley = 7;
    shot.shotSequence = 9;
    update({shot});
    auto starts = mixer.starts();
    shot.id = 80;
    update({shot});
    update({shot});
    expect(mixer.starts() == starts, "Prediction plus authoritative resend produces one own shot");
    shot.volley = 8;
    shot.shotSequence = 0;
    shot.id = 81;
    update({shot});
    expect(mixer.starts() == starts + 1, "Next automatic-fire volley is not swallowed");
    auto old = shot;
    players[0].lifeRevision = 1;
    shot.ownerLife = 1;
    shot.volley = 7;
    shot.id = 82;
    update({shot, old});
    expect(mixer.starts() == starts + 2, "New life can shoot; late old-life events stay silent");
    GameEvent active;
    active.round = 1;
    active.kind = EventKind::ActiveCast;
    active.actor = 1;
    active.ownerLife = 1;
    active.active = ActiveKind::Parry;
    active.shotSequence = 22;
    active.radius = 0;
    update({active});
    starts = mixer.starts();
    active.id = 83;
    update({active});
    expect(mixer.starts() == starts, "Predicted active is not replayed by confirmation");
    finish(mixer);
    starts = mixer.starts();
    players[0].weapon.ammo = 1;
    players[0].weapon.startReload();
    update();
    expect(mixer.starts() == starts + 1, "Reload starts from state even without a network event");
    finish(mixer);
    starts = mixer.starts();
    players[0].position.x += 65;
    players[0].velocity.x = 200;
    update();
    expect(mixer.starts() == starts + 1, "Ground movement gives a distance-based footstep");
    starts = mixer.starts();
    players[0].position.x += 300;
    players[0].lifeRevision++;
    update();
    expect(mixer.starts() == starts, "Respawn does not create a fake footstep or landing");
    scene.clear();
    mixer.clear();
    players[0].health = 100;
    players[1].health = 100;
    GameEvent hit;
    hit.id = 100;
    hit.kind = EventKind::Hit;
    hit.actor = 1;
    hit.target = 2;
    hit.targetLife = players[1].lifeRevision;
    starts = mixer.starts();
    update({hit});
    auto accepted = mixer.starts();
    update({hit});
    expect(accepted > starts && accepted == mixer.starts(), "Hit feedback is authoritative and deduplicated");
}

void device()
{
#ifdef __linux__
    bool realDriver = false;
    for (int i = 0; i < SDL_GetNumAudioDrivers(); ++i)
    {
        const std::string driver(SDL_GetAudioDriver(i));
        realDriver = realDriver || driver == "alsa" || driver == "pulseaudio" || driver == "pipewire";
    }
    expect(realDriver, "Linux SDL must include a real audio backend, not only dummy playback");
#endif
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    AudioSystem audio;
    expect(audio.initialize(bank()) && audio.available(), "SDL dummy device opens real audio stream");
    audio.mixer.play(SoundCue::Click);
    audio.pump(true);
    audio.pump(false);
    expect(audio.mixer.voices() == 0, "Focus loss clears queued playback and active voices");
    audio.pump(true);
    audio.shutdown();
    expect(!audio.available(), "Device shuts down before SDL_Quit");
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "cubeshot-unavailable-test-driver");
    expect(!audio.initialize(bank()) && !audio.available(), "Missing audio device is a nonfatal condition");
}
} // namespace

int main()
{
    try
    {
        expect(SDL_Init(0), "Initialize SDL");
        samplesAndMix();
        eventDedupAndMovement();
        device();
        SDL_Quit();
        std::cout << "Audio bank, bounded mix, deduplication, settings and device fallback passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        SDL_Quit();
        return 1;
    }
}
