#!/usr/bin/env bash
set -euo pipefail
export HOME="${HOME:-/home/nicol}"
if [[ "$HOME" == *":"* ]] || [[ "$HOME" == C* ]]; then
    export HOME=/home/nicol
fi
export DISPLAY="${DISPLAY:-:0}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export PULSE_SERVER="${PULSE_SERVER:-unix:/mnt/wslg/PulseServer}"
if [ -d /mnt/wslg/runtime-dir ]; then
    export XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=linux-deps.sh
. "$ROOT/scripts/linux-deps.sh"

echo "HOME=$HOME"
echo "DISPLAY=$DISPLAY"
echo "USER=$(whoami)"
echo "PWD=$(pwd)"
ls /home/nicol >/dev/null

req="$(dawn_linux_missing_required | tr '\n' ' ')"
run="$(dawn_linux_missing_runtime | tr '\n' ' ')"
if [ -z "${req// /}" ]; then
    echo "build_packages=ok"
else
    echo "build_packages=missing ${req}"
fi
if [ -z "${run// /}" ]; then
    echo "runtime_packages=ok"
else
    echo "runtime_packages=missing ${run}"
fi
if [ -S /tmp/.X11-unix/X0 ] || [ -S /mnt/wslg/.X11-unix/X0 ]; then
    echo "x11_socket=yes"
else
    echo "x11_socket=no"
fi
if command -v xdpyinfo >/dev/null 2>&1; then
    xdpyinfo >/dev/null 2>&1 && echo "xdpyinfo=ok" || echo "xdpyinfo=fail"
else
    echo "xdpyinfo=missing"
fi
