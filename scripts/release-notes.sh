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
Dawn is no longer bundled in the launcher. Install and Update always fetch the latest Dawn release from GitHub, so an older overlay cannot stay behind.

If the installed Dawn version is outdated, Play becomes Update. That replace wipes the old Dawn files first and writes the new version on top.
EOF
else
    cat > "$OUT" <<EOF
Latest automatic build from \`main\`. This prerelease is replaced every time new code is pushed.

Use a \`v*\` tag when you want a fixed official version that stays published.
EOF
fi
