#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM="${1:-}"
DD_TAG="DepotDownloader_3.4.0"
DIST="$ROOT/dist"
STAGE="$DIST/Dawn"

if [ "$PLATFORM" != "windows" ] && [ "$PLATFORM" != "linux" ]; then
    echo "usage: package-ci.sh windows|linux" >&2
    exit 1
fi

if [ "$PLATFORM" = "windows" ]; then
    HOST="$ROOT/build/host.exe"
    LOGIC="$ROOT/build/app_logic.dll"
    if [ ! -f "$HOST" ]; then
        HOST="$ROOT/build/Release/host.exe"
        LOGIC="$ROOT/build/Release/app_logic.dll"
    fi
    DD_URL="https://github.com/SteamRE/DepotDownloader/releases/download/${DD_TAG}/DepotDownloader-windows-x64.zip"
    DD_NAME="DepotDownloader.exe"
    LAUNCHER_NAME="Dawn.exe"
    ARCHIVE="$DIST/Dawn-windows-x64.zip"
else
    HOST="$ROOT/build-linux/host"
    LOGIC="$ROOT/build-linux/app_logic.so"
    if [ ! -f "$HOST" ]; then
        HOST="$ROOT/build/host"
        LOGIC="$ROOT/build/app_logic.so"
    fi
    DD_URL="https://github.com/SteamRE/DepotDownloader/releases/download/${DD_TAG}/DepotDownloader-linux-x64.zip"
    DD_NAME="DepotDownloader"
    LAUNCHER_NAME="Dawn"
    ARCHIVE="$DIST/Dawn-linux-x64.tar.gz"
fi

if [ ! -f "$HOST" ] || [ ! -f "$LOGIC" ]; then
    echo "missing launcher binaries: $HOST / $LOGIC" >&2
    exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/tools/DepotDownloader" "$STAGE/themes"

cp "$HOST" "$STAGE/$LAUNCHER_NAME"
cp "$LOGIC" "$STAGE/"
cp -R "$ROOT/themes/." "$STAGE/themes/"

if [ -f "$ROOT/packaging/Dawn" ] && [ "$PLATFORM" = "linux" ]; then
    cp "$ROOT/packaging/Dawn" "$STAGE/Dawn.sh"
    chmod +x "$STAGE/Dawn.sh"
fi

DD_ZIP="$DIST/depotdownloader.zip"
mkdir -p "$DIST"
curl -fsSL "$DD_URL" -o "$DD_ZIP"
python - "$DD_ZIP" "$STAGE/tools/DepotDownloader" <<'PY'
import sys, zipfile
zip_path, dest = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path) as zf:
    zf.extractall(dest)
PY
rm -f "$DD_ZIP"

if [ ! -f "$STAGE/tools/DepotDownloader/$DD_NAME" ]; then
    found="$(find "$STAGE/tools/DepotDownloader" -name "$DD_NAME" | head -n 1 || true)"
    if [ -n "${found:-}" ]; then
        mv "$found" "$STAGE/tools/DepotDownloader/$DD_NAME"
    fi
fi

if [ ! -f "$STAGE/tools/DepotDownloader/$DD_NAME" ]; then
    echo "DepotDownloader binary missing after extract" >&2
    exit 1
fi

VERSION="${APP_VERSION:-}"
if [ -z "$VERSION" ] && [ -f "$ROOT/VERSION" ]; then
    VERSION="$(tr -d ' \t\r\n' < "$ROOT/VERSION")"
fi
if [ -n "$VERSION" ]; then
    printf '%s\n' "$VERSION" > "$STAGE/VERSION"
fi

chmod +x "$STAGE/$LAUNCHER_NAME" "$STAGE/tools/DepotDownloader/$DD_NAME" || true

rm -f "$ARCHIVE"
if [ "$PLATFORM" = "windows" ]; then
    python - "$STAGE" "$ARCHIVE" <<'PY'
import os, sys, zipfile
root, archive = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
    for dirpath, _, filenames in os.walk(root):
        for name in filenames:
            full = os.path.join(dirpath, name)
            zf.write(full, os.path.relpath(full, root))
PY
else
    tar -C "$STAGE" -czf "$ARCHIVE" .
fi

echo "packed $ARCHIVE"
