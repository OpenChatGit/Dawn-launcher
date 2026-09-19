#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=wsl-env.sh
. "$ROOT/scripts/wsl-env.sh"
# shellcheck source=linux-deps.sh
. "$ROOT/scripts/linux-deps.sh"

BUILD="$ROOT/build-linux"
RUN=0
TYPE=Release
DEPS_ONLY=0

for arg in "$@"; do
    case "$arg" in
        --run) RUN=1 ;;
        --debug) TYPE=Debug ;;
        --release) TYPE=Release ;;
        --deps-only) DEPS_ONLY=1 ;;
    esac
done

dawn_linux_ensure_packages

if [ "$DEPS_ONLY" -eq 1 ]; then
    echo "Dependencies only; not building."
    exit 0
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
