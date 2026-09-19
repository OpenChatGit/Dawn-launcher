#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=wsl-env.sh
. "$ROOT/scripts/wsl-env.sh"

BUILD="$ROOT/build-linux"
RUN=0
TYPE=Release

for arg in "$@"; do
    case "$arg" in
        --run) RUN=1 ;;
        --debug) TYPE=Debug ;;
        --release) TYPE=Release ;;
    esac
done

need=()
command -v cmake >/dev/null || need+=(cmake)
command -v ninja >/dev/null || need+=(ninja-build)
command -v g++ >/dev/null || need+=(g++)
pkg-config --exists x11 2>/dev/null || need+=(libx11-dev)
pkg-config --exists zlib 2>/dev/null || need+=(zlib1g-dev)
dpkg -s pkg-config >/dev/null 2>&1 || need+=(pkg-config)

if [ "${#need[@]}" -gt 0 ]; then
    echo "Missing packages: ${need[*]}"
    echo "In Ubuntu WSL run:"
    echo "  sudo apt update"
    echo "  sudo apt install -y cmake ninja-build g++ pkg-config libx11-dev libcurl4-openssl-dev zlib1g-dev"
    exit 1
fi

if ! pkg-config --exists libcurl 2>/dev/null; then
    echo "Note: libcurl4-openssl-dev is missing. UI will build; Steam HTTP needs:"
    echo "  sudo apt install -y libcurl4-openssl-dev"
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE"
cmake --build "$BUILD" --target host --target app_logic

echo
echo "Linux build is ready:"
echo "  $BUILD/host"
echo "  DISPLAY=$DISPLAY HOME=$HOME"

if [ "$RUN" -eq 1 ]; then
    if [ ! -S /tmp/.X11-unix/X0 ] && [ ! -S /mnt/wslg/.X11-unix/X0 ]; then
        echo "No WSLg X11 socket. Open Ubuntu from Windows Terminal, or install WSLg (Windows 11)."
        exit 1
    fi
    cd "$ROOT"
    exec "$BUILD/host"
fi

echo
echo "Run it from WSL:"
echo "  $ROOT/scripts/build-wsl.sh --run"
echo "or from Windows:"
echo "  .\\scripts\\dev-wsl.ps1"
