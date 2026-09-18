#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-linux"

if [ -z "${HOME:-}" ] || [ ! -d "${HOME:-}" ]; then
    if [ -d /home/nicol ]; then
        export HOME=/home/nicol
    else
        export HOME="${HOME:-/tmp}"
    fi
fi

if ! command -v cmake >/dev/null || ! command -v ninja >/dev/null || ! command -v g++ >/dev/null; then
    echo "Missing build tools. In Ubuntu WSL run:"
    echo "  sudo apt update"
    echo "  sudo apt install -y cmake ninja-build g++ pkg-config libx11-dev libcurl4-openssl-dev"
    exit 1
fi

cmake -S "$ROOT" -B "$BUILD" -G Ninja
cmake --build "$BUILD" --target host --target app_logic

echo
echo "Linux build is ready:"
echo "  $BUILD/host"
echo
echo "Run it from WSL (needs a display, WSLg or an X server):"
echo "  cd \"$ROOT\""
echo "  \"$BUILD/host\""
