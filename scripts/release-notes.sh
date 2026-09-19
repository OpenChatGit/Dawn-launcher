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
Linux Play uses only Wine/DXVK already on disk (Lutris wine-tkg 11.9 and DXVK 2.6.2 first). Dawn does not download a Wine build or DXVK.

It reuses an existing Lutris Destiny prefix when present, or a Dawn prefix that already has DXVK. System Wine without DXVK is not used. steam-run is gone so \`%AppData%\` stays valid.

Proton stays as a fallback with a Dawn compat path, not Steam compatdata 1085660.
EOF
else
    cat > "$OUT" <<EOF
Latest automatic build from \`main\`. This prerelease is replaced every time new code is pushed.

Use a \`v*\` tag when you want a fixed official version that stays published.
EOF
fi
