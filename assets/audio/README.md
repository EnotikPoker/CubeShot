# CubeShot sound bank

Original procedural sounds created for CubeShot; no third-party recordings,
samples, synthesizer presets or external Python packages are used.
Reproduce every WAV byte with `python3 tools/generate_audio.py`.
The generator uses fixed seeds, envelopes, filtered noise and oscillators.

Format: mono signed 16-bit little-endian PCM, 48 kHz. Playback mixes into
stereo with position, per-category gain, priority limits and a soft limiter.
This bank covers all 21 weapons and shared combat/interface cues. There is
no music in this version. Source and generation parameters remain available
for further sound design and replacement by recorded sounds.
