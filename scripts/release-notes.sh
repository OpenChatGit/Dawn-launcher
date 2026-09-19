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
**New Version** in the titlebar when a newer official build is available. Dev builds still show the control so it can be tested.

Steam password is kept only until the Destiny 2 depot download finishes, then deleted. DepotDownloader sign-in appears only when an install is still needed; otherwise you just use Steam sign-in.

Steam OpenID now says **Sign in to Dawn**. The browser callback is a Dawn page with the emblem.

Hover and tooltip for the update control stay compact: icon plus **New Version**, version pill to the left, no fade-out.
EOF
else
    cat > "$OUT" <<EOF
Latest automatic build from \`main\`. This prerelease is replaced every time new code is pushed.

Use a \`v*\` tag when you want a fixed official version that stays published.
EOF
fi
