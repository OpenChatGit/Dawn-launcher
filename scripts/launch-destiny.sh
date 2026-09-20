#!/usr/bin/env sh
# Same Linux launch path as Dawn-installer: steam-run + Proton, else Wine.
set -e

GAME_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$GAME_DIR"
export DAWN_FOREST_BASELINE=1
unset SteamAppId SteamGameId SteamOverlayGameId
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-d3d8,d3d9,d3d10core,d3d11,dxgi=n,b}"

EXE="${DAWN_GAME_EXE:-$GAME_DIR/destiny2.exe}"

RUNNER=""
if command -v steam-run >/dev/null 2>&1; then
    RUNNER="steam-run"
elif [ -x "/run/current-system/sw/bin/steam-run" ]; then
    RUNNER="/run/current-system/sw/bin/steam-run"
fi

PROTON_CANDIDATE=""
for cand in \
    "$HOME/.local/share/Steam/steamapps/common/Proton - Experimental/proton" \
    "$HOME/.local/share/Steam/steamapps/common/Proton 10.0/proton" \
    "$HOME/.local/share/Steam/steamapps/common/Proton 9.0/proton" \
    "$HOME/.local/share/Steam/steamapps/common/Proton 8.0/proton" \
    "$HOME/.steam/steam/steamapps/common/Proton - Experimental/proton" \
    "$HOME/.steam/steam/steamapps/common/Proton 9.0/proton" \
    "$HOME/.steam/root/steamapps/common/Proton - Experimental/proton" \
    "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Proton - Experimental/proton"
do
    if [ -f "$cand" ]; then
        PROTON_CANDIDATE="$cand"
        break
    fi
done

if [ -n "$PROTON_CANDIDATE" ]; then
    STEAM_ROOT="$(dirname "$(dirname "$(dirname "$(dirname "$PROTON_CANDIDATE")")")")"
    export STEAM_COMPAT_CLIENT_INSTALL_PATH="$STEAM_ROOT"
    export STEAM_COMPAT_DATA_PATH="$STEAM_ROOT/steamapps/compatdata/1085660"
    mkdir -p "$STEAM_COMPAT_DATA_PATH"
    echo "[Dawn] proton=$PROTON_CANDIDATE" >&2
    if [ -n "$RUNNER" ]; then
        echo "[Dawn] runner=$RUNNER" >&2
        exec $RUNNER "$PROTON_CANDIDATE" run "$EXE" "$@"
    fi
    exec "$PROTON_CANDIDATE" run "$EXE" "$@"
fi

if command -v wine64 >/dev/null 2>&1; then
    echo "[Dawn] wine=wine64" >&2
    if [ -n "$RUNNER" ]; then
        exec $RUNNER wine64 "$EXE" "$@"
    fi
    exec wine64 "$EXE" "$@"
fi
if command -v wine >/dev/null 2>&1; then
    echo "[Dawn] wine=wine" >&2
    if [ -n "$RUNNER" ]; then
        exec $RUNNER wine "$EXE" "$@"
    fi
    exec wine "$EXE" "$@"
fi

echo "[ERROR] Neither Proton nor Wine was found." >&2
echo "Install Proton in Steam, or Wine, then run this script again." >&2
exit 1
