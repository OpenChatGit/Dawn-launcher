#!/usr/bin/env sh
# Destiny 2 Linux launcher for Dawn.
# Prefers Lutris wine-tkg + DXVK (tester setup: wine-tkg 11.9, DXVK 2.6.2).
# Does not wrap Wine in steam-run — that makes `%AppData%` empty.
set -e

GAME_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$GAME_DIR"
export DAWN_FOREST_BASELINE=1
unset SteamAppId SteamGameId SteamOverlayGameId

if [ -z "${HOME:-}" ]; then
    HOME="$(getent passwd "$(id -un)" 2>/dev/null | cut -d: -f6 || true)"
    export HOME
fi

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
PREFIX="${DAWN_WINEPREFIX:-$DATA_HOME/Dawn/wineprefix}"

wine_from_dir() {
    d="$1"
    if [ -x "$d/bin/wine64" ]; then
        printf '%s\n' "$d/bin/wine64"
        return 0
    fi
    if [ -x "$d/bin/wine" ]; then
        printf '%s\n' "$d/bin/wine"
        return 0
    fi
    return 1
}

scan_wine_base() {
    base="$1"
    filter="$2"
    [ -d "$base" ] || return 1
    for d in "$base"/*; do
        [ -d "$d" ] || continue
        name="${d##*/}"
        case "$filter" in
        tkg11)
            case "$name" in
            *11.9*[Tt][Kk][Gg]*|*[Tt][Kk][Gg]*11.9*) ;;
            *) continue ;;
            esac
            ;;
        tkg)
            case "$name" in
            *[Tt][Kk][Gg]*) ;;
            *) continue ;;
            esac
            ;;
        esac
        wine_from_dir "$d" && return 0
    done
    return 1
}

pick_lutris_wine() {
    if [ -n "${DAWN_WINE:-}" ] && [ -x "$DAWN_WINE" ]; then
        printf '%s\n' "$DAWN_WINE"
        return 0
    fi
    set -- \
        "$HOME/.local/share/lutris/runners/wine" \
        "$HOME/.var/app/net.lutris.Lutris/data/lutris/runners/wine" \
        "/usr/share/lutris/runners/wine"
    for base in "$@"; do
        scan_wine_base "$base" tkg11 && return 0
    done
    for base in "$@"; do
        scan_wine_base "$base" tkg && return 0
    done
    for base in "$@"; do
        scan_wine_base "$base" any && return 0
    done
    return 1
}

dxvk_ok() {
    [ -f "$1/x64/d3d11.dll" ] || [ -f "$1/x64/dxgi.dll" ]
}

scan_dxvk_base() {
    base="$1"
    prefer="$2"
    [ -d "$base" ] || return 1
    if [ "$prefer" = 1 ]; then
        for d in "$base/v2.6.2" "$base/2.6.2" "$base/dxvk-2.6.2"; do
            if dxvk_ok "$d"; then
                printf '%s\n' "$d"
                return 0
            fi
        done
        return 1
    fi
    for d in "$base"/*; do
        if dxvk_ok "$d"; then
            printf '%s\n' "$d"
            return 0
        fi
    done
    return 1
}

pick_dxvk() {
    if [ -n "${DAWN_DXVK:-}" ] && dxvk_ok "$DAWN_DXVK"; then
        printf '%s\n' "$DAWN_DXVK"
        return 0
    fi
    set -- \
        "$HOME/.local/share/lutris/runtime/dxvk" \
        "$HOME/.var/app/net.lutris.Lutris/data/lutris/runtime/dxvk" \
        "/usr/share/lutris/runtime/dxvk" \
        "/usr/share/dxvk" \
        "/usr/lib/dxvk"
    for base in "$@"; do
        scan_dxvk_base "$base" 1 && return 0
    done
    for base in "$@"; do
        scan_dxvk_base "$base" 0 && return 0
    done
    return 1
}

install_dxvk() {
    dxvk="$1"
    sys32="$PREFIX/drive_c/windows/system32"
    sys64="$PREFIX/drive_c/windows/syswow64"
    mkdir -p "$sys32"
    for dll in d3d8.dll d3d9.dll d3d10core.dll d3d11.dll d3d12.dll dxgi.dll; do
        if [ -f "$dxvk/x64/$dll" ]; then
            cp -f "$dxvk/x64/$dll" "$sys32/$dll"
        fi
        if [ -f "$dxvk/x32/$dll" ]; then
            mkdir -p "$sys64"
            cp -f "$dxvk/x32/$dll" "$sys64/$dll"
        fi
    done
}

ensure_prefix() {
    wine="$1"
    mkdir -p "$PREFIX"
    export WINEPREFIX="$PREFIX"
    export WINEDEBUG="${WINEDEBUG:--all}"
    export WINEESYNC="${WINEESYNC:-1}"
    export WINEFSYNC="${WINEFSYNC:-1}"
    if [ ! -d "$PREFIX/drive_c/users" ]; then
        boot="$(dirname "$wine")/wineboot"
        if [ -x "$boot" ]; then
            WINEPREFIX="$PREFIX" "$boot" -u >/dev/null 2>&1 || true
        else
            WINEPREFIX="$PREFIX" "$wine" wineboot -u >/dev/null 2>&1 || true
        fi
    fi
}

run_wine_game() {
    wine="$1"
    shift
    echo "[Dawn] wine=$wine" >&2
    echo "[Dawn] prefix=$PREFIX" >&2
    ensure_prefix "$wine"
    if DXVK="$(pick_dxvk)"; then
        echo "[Dawn] dxvk=$DXVK" >&2
        install_dxvk "$DXVK"
        if [ -z "${WINEDLLOVERRIDES:-}" ]; then
            export WINEDLLOVERRIDES="d3d8,d3d9,d3d10core,d3d11,dxgi=n,b"
        fi
    else
        echo "[Dawn] dxvk=missing (install Lutris DXVK 2.6.2)" >&2
    fi
    exec "$wine" "$GAME_DIR/destiny2.exe" "$@"
}

if [ -n "${DAWN_LUTRIS_GAME:-}" ] && command -v lutris >/dev/null 2>&1; then
    exec lutris "lutris:rungame/$DAWN_LUTRIS_GAME"
fi

if WINE="$(pick_lutris_wine)"; then
    run_wine_game "$WINE" "$@"
fi

for root in \
    "$HOME/.local/share/Steam" \
    "$HOME/.steam/steam" \
    "$HOME/.steam/root" \
    "$HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"
do
    [ -d "$root/steamapps/common" ] || continue
    for cand in \
        "$root/steamapps/common/Proton - Experimental/proton" \
        "$root/steamapps/common/Proton 10.0/proton" \
        "$root/steamapps/common/Proton 9.0/proton" \
        "$root/steamapps/common/Proton 8.0/proton"
    do
        if [ -f "$cand" ]; then
            export STEAM_COMPAT_CLIENT_INSTALL_PATH="$root"
            export STEAM_COMPAT_DATA_PATH="${DAWN_PROTON_COMPAT:-$DATA_HOME/Dawn/proton-compat}"
            mkdir -p "$STEAM_COMPAT_DATA_PATH"
            echo "[Dawn] proton=$cand" >&2
            echo "[Dawn] compat=$STEAM_COMPAT_DATA_PATH" >&2
            exec "$cand" run "$GAME_DIR/destiny2.exe" "$@"
        fi
    done
done

if command -v wine64 >/dev/null 2>&1; then
    run_wine_game "$(command -v wine64)" "$@"
fi
if command -v wine >/dev/null 2>&1; then
    run_wine_game "$(command -v wine)" "$@"
fi

echo "[ERROR] Neither Lutris wine-tkg, Proton, nor Wine was found." >&2
echo "Install Lutris wine-tkg 11.9 and DXVK 2.6.2, or set DAWN_WINE / DAWN_DXVK." >&2
exit 1
