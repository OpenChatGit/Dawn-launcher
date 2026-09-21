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
The launcher can switch its own UI language in General (English by default, plus German). User settings now show Forsaken and Shadowkeep ownership, and Install/Play only work when you are signed in and both required DLCs are confirmed.

The title shows the Dawn game version from GitHub so you can see current vs installed. The Settings row uses a gear icon. Simulate download and the F12 debug console stay out of production builds.
EOF
else
    cat > "$OUT" <<EOF
Latest automatic build from \`main\`. This prerelease is replaced every time new code is pushed.

Use a \`v*\` tag when you want a fixed official version that stays published.
EOF
fi
