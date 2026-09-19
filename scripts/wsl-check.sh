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

echo "HOME=$HOME"
echo "DISPLAY=$DISPLAY"
echo "USER=$(whoami)"
echo "PWD=$(pwd)"
ls /home/nicol >/dev/null
dpkg -s libcurl4-openssl-dev 2>/dev/null | grep Status || echo "libcurl4-openssl-dev: missing"
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
