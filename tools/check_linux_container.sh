#!/usr/bin/env bash
# Run inside a clean Ubuntu container with only archives and these scripts mounted.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends python3 xvfb xauth weston libegl1 libgl1-mesa-dri adwaita-icon-theme xkb-data
export LIBGL_ALWAYS_SOFTWARE=1
export SDL_RENDER_DRIVER=software
SDL_VIDEO_DRIVER=x11 xvfb-run -a python3 /checks/check_package.py \
    /packages/CubeShot-linux-x64.tar.gz --version "$1" --require-release
export XDG_RUNTIME_DIR
XDG_RUNTIME_DIR=$(mktemp -d)
chmod 700 "$XDG_RUNTIME_DIR"
weston_args=(--backend=headless-backend.so --socket=cubeshot-test --idle-time=0)
if weston --help 2>&1 | grep -q -- --renderer; then
    weston_args+=(--renderer=pixman)
else
    weston_args+=(--use-pixman)
fi
weston "${weston_args[@]}" > /tmp/weston.log 2>&1 &
weston_pid=$!
trap 'kill "$weston_pid" 2>/dev/null || true' EXIT
for attempt in {1..100}; do
    if [ -S "$XDG_RUNTIME_DIR/cubeshot-test" ]; then break; fi
    if ! kill -0 "$weston_pid" 2>/dev/null; then cat /tmp/weston.log; exit 1; fi
    sleep 0.1
done
SDL_VIDEO_DRIVER=wayland WAYLAND_DISPLAY=cubeshot-test python3 /checks/check_package.py \
    /packages/CubeShot-linux-x64.tar.gz --version "$1" --require-release
