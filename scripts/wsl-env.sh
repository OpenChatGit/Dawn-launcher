#!/usr/bin/env bash
# Fix Windows-injected env so Ubuntu WSL can show WSLg windows.

if [ -z "${HOME:-}" ] || [[ "${HOME}" == *":"* ]] || [[ "${HOME}" == C* ]] || [[ "${HOME}" == /mnt/c/Users/* ]]; then
    if [ -d "/home/${USER:-nicol}" ]; then
        export HOME="/home/${USER:-nicol}"
    elif [ -d /home/nicol ]; then
        export HOME=/home/nicol
    fi
fi

export DISPLAY="${DISPLAY:-:0}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
if [ -d /mnt/wslg/runtime-dir ]; then
    export XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir
fi
if [ -S /mnt/wslg/PulseServer ]; then
    export PULSE_SERVER="${PULSE_SERVER:-unix:/mnt/wslg/PulseServer}"
fi
