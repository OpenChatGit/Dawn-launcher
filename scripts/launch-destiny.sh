#!/usr/bin/env sh
# Destiny 2 Linux launcher for Dawn.
# Uses only Wine/DXVK/Proton that are already on disk (Lutris wine-tkg + DXVK).
# Never downloads runners, DXVK, Gecko, or Mono.
set -e

GAME_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$GAME_DIR"
export DAWN_FOREST_BASELINE=1
unset SteamAppId SteamGameId SteamOverlayGameId

# wineboot must not fetch gecko/mono. DXVK overrides are added later if present.
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"

if [ -z "${HOME:-}" ]; then
    HOME="$(getent passwd "$(id -un)" 2>/dev/null | cut -d: -f6 || true)"
    export HOME
fi

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
PREFIX="${DAWN_WINEPREFIX:-$DATA_HOME/Dawn/wineprefix}"

log() {
    echo "[Dawn] $*" >&2
}

yaml_field() {
    file="$1"
    key="$2"
    [ -f "$file" ] || return 1
    line="$(grep -E "^[[:space:]]*${key}:[[:space:]]*" "$file" | head -n 1 || true)"
    [ -n "$line" ] || return 1
    val="${line#*:}"
    val="${val#"${val%%[![:space:]]*}"}"
    val="${val%"${val##*[![:space:]]}"}"
    val="${val#\"}"
    val="${val%\"}"
    val="${val#\'}"
    val="${val%\'}"
    [ -n "$val" ] || return 1
    printf '%s\n' "$val"
}

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

wine_from_name() {
    name="$1"
    [ -n "$name" ] || return 1
    for base in \
        "$HOME/.local/share/lutris/runners/wine" \
        "$HOME/.var/app/net.lutris.Lutris/data/lutris/runners/wine" \
        "/usr/share/lutris/runners/wine"
    do
        if [ -d "$base/$name" ]; then
            wine_from_dir "$base/$name" && return 0
        fi
    done
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
    for base in \
        "$HOME/.local/share/lutris/runners/wine" \
        "$HOME/.var/app/net.lutris.Lutris/data/lutris/runners/wine" \
        "/usr/share/lutris/runners/wine"
    do
        scan_wine_base "$base" tkg11 && return 0
    done
    for base in \
        "$HOME/.local/share/lutris/runners/wine" \
        "$HOME/.var/app/net.lutris.Lutris/data/lutris/runners/wine" \
        "/usr/share/lutris/runners/wine"
    do
        scan_wine_base "$base" tkg && return 0
    done
    for base in \
        "$HOME/.local/share/lutris/runners/wine" \
        "$HOME/.var/app/net.lutris.Lutris/data/lutris/runners/wine" \
        "/usr/share/lutris/runners/wine"
    do
        scan_wine_base "$base" any && return 0
    done
    return 1
}

dxvk_ok() {
    [ -f "$1/x64/d3d11.dll" ] || [ -f "$1/x64/dxgi.dll" ]
}

prefix_has_dxvk() {
    prefix="${1:-$PREFIX}"
    [ -f "$prefix/drive_c/windows/system32/d3d11.dll" ] &&
        [ -f "$prefix/drive_c/windows/system32/dxgi.dll" ]
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
    for base in \
        "$HOME/.local/share/lutris/runtime/dxvk" \
        "$HOME/.var/app/net.lutris.Lutris/data/lutris/runtime/dxvk" \
        "/usr/share/lutris/runtime/dxvk" \
        "/usr/share/dxvk" \
        "/usr/lib/dxvk"
    do
        scan_dxvk_base "$base" 1 && return 0
    done
    for base in \
        "$HOME/.local/share/lutris/runtime/dxvk" \
        "$HOME/.var/app/net.lutris.Lutris/data/lutris/runtime/dxvk" \
        "/usr/share/lutris/runtime/dxvk" \
        "/usr/share/dxvk" \
        "/usr/lib/dxvk"
    do
        scan_dxvk_base "$base" 0 && return 0
    done
    return 1
}

have_local_dxvk() {
    prefix_has_dxvk "$PREFIX" && return 0
    pick_dxvk >/dev/null && return 0
    return 1
}

apply_local_dxvk() {
    if prefix_has_dxvk "$PREFIX"; then
        log "dxvk=already in prefix"
        export WINEDLLOVERRIDES="d3d8,d3d9,d3d10core,d3d11,dxgi=n,b;mscoree,mshtml="
        return 0
    fi
    DXVK="$(pick_dxvk)" || return 1
    log "dxvk=$DXVK (local copy into prefix)"
    sys32="$PREFIX/drive_c/windows/system32"
    sys64="$PREFIX/drive_c/windows/syswow64"
    mkdir -p "$sys32"
    for dll in d3d8.dll d3d9.dll d3d10core.dll d3d11.dll d3d12.dll dxgi.dll; do
        if [ -f "$DXVK/x64/$dll" ]; then
            cp -f "$DXVK/x64/$dll" "$sys32/$dll"
        fi
        if [ -f "$DXVK/x32/$dll" ]; then
            mkdir -p "$sys64"
            cp -f "$DXVK/x32/$dll" "$sys64/$dll"
        fi
    done
    export WINEDLLOVERRIDES="d3d8,d3d9,d3d10core,d3d11,dxgi=n,b;mscoree,mshtml="
    return 0
}

ensure_prefix() {
    wine="$1"
    mkdir -p "$PREFIX"
    export WINEPREFIX="$PREFIX"
    export WINEDEBUG="${WINEDEBUG:--all}"
    export WINEESYNC="${WINEESYNC:-1}"
    export WINEFSYNC="${WINEFSYNC:-1}"
    if [ -d "$PREFIX/drive_c/users" ]; then
        return 0
    fi
    log "prefix init (no download)"
    boot="$(dirname "$wine")/wineboot"
    if [ -x "$boot" ]; then
        WINEPREFIX="$PREFIX" WINEDLLOVERRIDES="mscoree,mshtml=" "$boot" -u >/dev/null 2>&1 || true
    else
        WINEPREFIX="$PREFIX" WINEDLLOVERRIDES="mscoree,mshtml=" "$wine" wineboot -u >/dev/null 2>&1 || true
    fi
}

# Reuse a Lutris game that already points at this destiny2.exe.
use_lutris_game_if_present() {
    for dir in \
        "$HOME/.config/lutris/games" \
        "$HOME/.var/app/net.lutris.Lutris/config/lutris/games"
    do
        [ -d "$dir" ] || continue
        for f in "$dir"/*.yml "$dir"/*.yaml; do
            [ -f "$f" ] || continue
            exe="$(yaml_field "$f" exe || true)"
            case "$exe" in
            "$GAME_DIR/destiny2.exe"|"$GAME_DIR/Destiny2.exe") ;;
            *)
                echo "$exe" | grep -qiE 'destiny2\.exe' || continue
                ;;
            esac
            pref="$(yaml_field "$f" prefix || yaml_field "$f" wineprefix || true)"
            ver="$(yaml_field "$f" version || true)"
            wine=""
            game_prefix="${pref:-$PREFIX}"
            if [ -n "$ver" ]; then
                wine="$(wine_from_name "$ver" || true)"
            fi
            if [ -z "$wine" ]; then
                wine="$(pick_lutris_wine || true)"
            fi
            if [ -z "$wine" ] || [ ! -x "$wine" ]; then
                continue
            fi
            if ! prefix_has_dxvk "$game_prefix" && ! pick_dxvk >/dev/null; then
                log "skip lutris game $(basename "$f"): no local DXVK"
                continue
            fi
            PREFIX="$game_prefix"
            log "lutris-game=$(basename "$f")"
            run_wine_game "$wine" "$@"
        done
    done
    return 1
}

run_wine_game() {
    wine="$1"
    shift
    if ! have_local_dxvk; then
        log "wine=$wine but no local DXVK; not launching without it"
        return 1
    fi
    log "wine=$wine"
    log "prefix=$PREFIX"
    ensure_prefix "$wine"
    apply_local_dxvk || {
        log "DXVK apply failed"
        return 1
    }
    exec "$wine" "$GAME_DIR/destiny2.exe" "$@"
}

if [ -n "${DAWN_LUTRIS_GAME:-}" ] && command -v lutris >/dev/null 2>&1; then
    log "DAWN_LUTRIS_GAME set; handing off to existing Lutris profile"
    exec lutris "lutris:rungame/$DAWN_LUTRIS_GAME"
fi

use_lutris_game_if_present "$@" || true

if WINE="$(pick_lutris_wine)"; then
    if have_local_dxvk; then
        run_wine_game "$WINE" "$@"
    else
        log "found $WINE but no local DXVK under Lutris runtime; not downloading"
    fi
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
            log "proton=$cand (already installed, built-in DXVK)"
            log "compat=$STEAM_COMPAT_DATA_PATH"
            exec "$cand" run "$GAME_DIR/destiny2.exe" "$@"
        fi
    done
done

if have_local_dxvk; then
    if command -v wine64 >/dev/null 2>&1; then
        run_wine_game "$(command -v wine64)" "$@"
    fi
    if command -v wine >/dev/null 2>&1; then
        run_wine_game "$(command -v wine)" "$@"
    fi
fi

echo "[ERROR] No already-installed Wine+DXVK (or Proton) was found." >&2
echo "Dawn does not download Wine or DXVK. Install Lutris wine-tkg 11.9 and DXVK 2.6.2, then Play again." >&2
echo "Or set DAWN_WINE / DAWN_DXVK / DAWN_WINEPREFIX to paths you already have." >&2
exit 1
