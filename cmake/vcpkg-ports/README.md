# Pinned SDL desktop audio overlay

The SDL3 port, metadata, patch and usage file come from microsoft/vcpkg commit
`e456309491fd875487bf02b8a0dca76c1b1b00cc` (MIT; see LICENSE.txt).
SDL itself remains pinned to 3.4.18 with the upstream download SHA512.

CubeShot's only port change enables SDL_PULSEAUDIO on Linux. The stock port
explicitly disables PulseAudio and PipeWire, while its default features omit
ALSA. CubeShot enables the existing ALSA feature in its manifest, too.
Windows keeps the stock backend configuration.

Linux build hosts need `libpulse-dev` (Ubuntu). Like the upstream X11/Wayland
features, the PulseAudio build uses host development headers. The player
archive bundles libpulse/libasound and their non-system dependencies, so
players do not install development packages. Audio tests reject a Linux SDL
build with only dummy/disk drivers. Systems with no audio device can still play.
