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
Settings now uses a sidebar (User, General, Installation). Windows UI text uses Segoe UI with GDI ClearType, and the window follows per-monitor DPI.

Linux still uses IBM Plex and the same Install, Play, and uninstall flows as Windows.
EOF
else
    cat > "$OUT" <<EOF
Latest automatic build from \`main\`. This prerelease is replaced every time new code is pushed.

Use a \`v*\` tag when you want a fixed official version that stays published.
EOF
fi
