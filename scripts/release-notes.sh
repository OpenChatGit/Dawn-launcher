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
Linux Play now prefers Lutris wine-tkg (11.9) plus DXVK 2.6.2 instead of system Wine without Vulkan. That was why the game rendered wrong.

The launcher uses its own WINEPREFIX under Dawn and runs wineboot first, so \`%AppData%\` is a real path. steam-run is no longer wrapping Wine (that emptied AppData even when a manual \`wine\` command worked).

Proton stays as a fallback with a Dawn compat path, not Steam compatdata 1085660.
EOF
else
    cat > "$OUT" <<EOF
Latest automatic build from \`main\`. This prerelease is replaced every time new code is pushed.

Use a \`v*\` tag when you want a fixed official version that stays published.
EOF
fi
