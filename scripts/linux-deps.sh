#!/usr/bin/env bash
# Check and install packages needed to build and run Dawn on Linux / WSL.
# Safe to source from build-wsl.sh. Set DAWN_SKIP_APT=1 to check only.

dawn_linux_have_cmd() {
    command -v "$1" >/dev/null 2>&1
}

dawn_linux_have_pkg() {
    dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'
}

dawn_linux_have_pc() {
    dawn_linux_have_cmd pkg-config && pkg-config --exists "$1" 2>/dev/null
}

dawn_linux_wsl_exe() {
    if dawn_linux_have_cmd wsl.exe; then
        command -v wsl.exe
        return 0
    fi
    if [ -x /mnt/c/Windows/System32/wsl.exe ]; then
        echo /mnt/c/Windows/System32/wsl.exe
        return 0
    fi
    return 1
}

dawn_linux_apt() {
    local wsl=""
    if [ "$(id -u)" -eq 0 ]; then
        DEBIAN_FRONTEND=noninteractive "$@"
        return
    fi
    if sudo -n true >/dev/null 2>&1; then
        DEBIAN_FRONTEND=noninteractive sudo -n "$@"
        return
    fi
    if wsl=$(dawn_linux_wsl_exe) && [ -n "${WSL_DISTRO_NAME:-}" ]; then
        "$wsl" -d "$WSL_DISTRO_NAME" -u root -- env DEBIAN_FRONTEND=noninteractive "$@"
        return
    fi
    if [ -t 0 ] && [ -t 1 ]; then
        DEBIAN_FRONTEND=noninteractive sudo "$@"
        return
    fi
    echo "Need sudo to install packages. Run this in a WSL terminal, or enable passwordless sudo:"
    echo "  sudo $*"
    return 1
}

# Compile + UI + Steam HTTP. Build stops if these stay missing.
dawn_linux_missing_required() {
    dawn_linux_have_cmd cmake || echo cmake
    dawn_linux_have_cmd ninja || echo ninja-build
    dawn_linux_have_cmd g++ || echo g++
    dawn_linux_have_cmd pkg-config || echo pkg-config
    dawn_linux_have_cmd git || echo git
    dawn_linux_have_cmd curl || echo curl
    dawn_linux_have_cmd python3 || echo python3
    dawn_linux_have_pkg ca-certificates || echo ca-certificates
    dawn_linux_have_pc x11 || echo libx11-dev
    dawn_linux_have_pc zlib || echo zlib1g-dev
    dawn_linux_have_pc libcurl || echo libcurl4-openssl-dev
    dawn_linux_have_cmd xdg-open || echo xdg-utils
}

# Install / Play of Windows binaries. Best-effort.
dawn_linux_missing_runtime() {
    if ! dawn_linux_have_cmd wine64 && ! dawn_linux_have_cmd wine; then
        echo wine64
    fi
    if ! dawn_linux_have_cmd dotnet; then
        echo dotnet-runtime-8.0
    fi
}

dawn_linux_collect() {
    local line
    while IFS= read -r line; do
        [ -n "$line" ] && printf '%s\n' "$line"
    done
}

dawn_linux_pkg_candidate() {
    local cand
    cand=$(apt-cache policy "$1" 2>/dev/null | awk '/Candidate:/ {print $2; exit}')
    [ -n "$cand" ] && [ "$cand" != "(none)" ]
}

dawn_linux_pick_pkg() {
    local candidate
    for candidate in "$@"; do
        if dawn_linux_pkg_candidate "$candidate"; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

dawn_linux_resolve() {
    local pkg picked
    for pkg in "$@"; do
        case "$pkg" in
            wine64)
                if picked=$(dawn_linux_pick_pkg wine64 wine); then
                    echo "$picked"
                else
                    echo "No wine package in apt; Play/Install of .exe needs wine or Proton." >&2
                fi
                ;;
            dotnet-runtime-8.0)
                if picked=$(dawn_linux_pick_pkg dotnet-runtime-8.0 dotnet-runtime-9.0 dotnet-runtime-10.0 dotnet-host-8.0 dotnet-host); then
                    echo "$picked"
                else
                    echo "No dotnet runtime in apt; DepotDownloader.exe will use wine if present." >&2
                fi
                ;;
            *)
                echo "$pkg"
                ;;
        esac
    done
}

dawn_linux_install_list() {
    local -a want=("$@")
    local pkg picked rc=0

    if [ "${#want[@]}" -eq 0 ]; then
        return 0
    fi

    for pkg in "${want[@]}"; do
        picked=$(dawn_linux_resolve "$pkg" || true)
        if [ -z "$picked" ]; then
            rc=1
            continue
        fi
        echo "Installing: $picked"
        if ! dawn_linux_apt apt-get install -y --no-install-recommends $picked; then
            echo "apt failed: $picked"
            rc=1
        fi
    done
    return "$rc"
}

dawn_linux_ensure_packages() {
    local -a required=() runtime=()
    local line

    mapfile -t required < <(dawn_linux_missing_required)
    mapfile -t runtime < <(dawn_linux_missing_runtime)

    if [ "${#required[@]}" -eq 0 ] && [ "${#runtime[@]}" -eq 0 ]; then
        echo "Linux packages: ok"
        return 0
    fi

    if [ "${#required[@]}" -gt 0 ]; then
        echo "Missing build packages: ${required[*]}"
    fi
    if [ "${#runtime[@]}" -gt 0 ]; then
        echo "Missing runtime packages: ${runtime[*]}"
    fi

    if [ "${DAWN_SKIP_APT:-0}" = "1" ]; then
        echo "DAWN_SKIP_APT=1; not installing."
        [ "${#required[@]}" -eq 0 ]
        return
    fi

    dawn_linux_apt apt-get update -y

    if [ "${#required[@]}" -gt 0 ]; then
        if ! dawn_linux_install_list "${required[@]}"; then
            echo "Failed to install build packages: ${required[*]}"
            echo "  sudo apt update && sudo apt install -y ${required[*]}"
            return 1
        fi
    fi

    if [ "${#runtime[@]}" -gt 0 ]; then
        if ! dawn_linux_install_list "${runtime[@]}"; then
            echo "Runtime packages were not installed. Install/Play of Windows .exe may fail."
            echo "  sudo apt install -y wine64"
        fi
    fi

    required=()
    mapfile -t required < <(dawn_linux_missing_required)
    if [ "${#required[@]}" -gt 0 ]; then
        echo "Still missing after apt: ${required[*]}"
        return 1
    fi

    runtime=()
    mapfile -t runtime < <(dawn_linux_missing_runtime)
    if [ "${#runtime[@]}" -gt 0 ]; then
        echo "Optional runtime still missing: ${runtime[*]}"
    else
        echo "Linux packages: ok"
    fi
    return 0
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    set -euo pipefail
    dawn_linux_ensure_packages
fi
