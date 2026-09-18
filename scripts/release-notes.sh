#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-}"
KIND="${2:-latest}"
OUT="${3:-release-notes.md}"
SHA="${GITHUB_SHA:-}"
SHORT="${SHA:0:7}"
DATE_UTC="$(date -u +"%Y-%m-%d %H:%M UTC")"

if [ -z "$VERSION" ]; then
    echo "usage: release-notes.sh VERSION official|latest [outfile]" >&2
    exit 1
fi

if [ "$KIND" = "official" ]; then
    cat > "$OUT" <<EOF
# Dawn ${VERSION}

Official Dawn launcher for **Windows** and **Linux**.

## Downloads

| Platform | Package | How to run |
| --- | --- | --- |
| Windows x64 | \`Dawn-windows-x64.zip\` | Unzip and run \`Dawn.exe\` |
| Linux x64 | \`Dawn-linux-x64.tar.gz\` | Extract, then \`chmod +x Dawn && ./Dawn\` |

Linux needs a display (WSLg or X11).

## This build

- **Version:** ${VERSION}
- **Commit:** \`${SHORT}\`
- **Published:** ${DATE_UTC}

Each package includes the native launcher, the Dawn theme, and DepotDownloader as a separate tool.

Steam and Bungie keys stay on your machine. \`.env\` is never packed into the release.
EOF
else
    cat > "$OUT" <<EOF
# Dawn ${VERSION}

Latest automatic build from \`main\`. This prerelease is replaced every time new code is pushed.

Use a \`v*\` tag when you want a fixed official version that stays published.

## Downloads

| Platform | Package | How to run |
| --- | --- | --- |
| Windows x64 | \`Dawn-windows-x64.zip\` | Unzip and run \`Dawn.exe\` |
| Linux x64 | \`Dawn-linux-x64.tar.gz\` | Extract, then \`chmod +x Dawn && ./Dawn\` |

Linux needs a display (WSLg or X11). You do not need to compile the launcher yourself.

## This build

- **Version:** ${VERSION}
- **Commit:** \`${SHORT}\`
- **Published:** ${DATE_UTC}

Each package includes the native launcher, the Dawn theme, and DepotDownloader as a separate tool.

Steam and Bungie keys stay on your machine. \`.env\` is never packed into the release.
EOF
fi

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    {
        echo
        echo "## Recent changes"
        echo
        git log -12 --pretty=format:'- %s (`%h`)'
        echo
    } >> "$OUT"
fi
