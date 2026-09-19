#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-}"
KIND="${2:-latest}"
OUT="${3:-release-notes.md}"

if [ -z "$VERSION" ]; then
    echo "usage: release-notes.sh VERSION official|latest [outfile]" >&2
    exit 1
fi

if [ "$KIND" = "official" ]; then
    cat > "$OUT" <<EOF
Linux now matches Windows for Install, Play, Stop, and uninstall (Dawn only, Sunrise only, or full). The Linux build installs missing packages, draws the Steam avatar, and asks for the depot password once.

WSL can build and open the Linux UI on the desktop with \`dev-wsl.ps1\` or \`build-wsl.sh --run\`.
EOF
else
    cat > "$OUT" <<EOF
Latest automatic build from \`main\`. This prerelease is replaced every time new code is pushed.

Use a \`v*\` tag when you want a fixed official version that stays published.
EOF
fi
