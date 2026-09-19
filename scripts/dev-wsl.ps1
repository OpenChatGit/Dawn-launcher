$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$distro = "Ubuntu"

$wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
if (-not $wsl) {
    Write-Error "wsl.exe not found. Install Windows Subsystem for Linux first."
}

$names = @(wsl.exe -l -q | ForEach-Object { $_.ToString().Trim([char]0).Trim() } | Where-Object { $_ })
if ($names -notcontains $distro) {
    Write-Error "WSL distro '$distro' not found. Installed: $($names -join ', ')"
}

$unixRoot = (wsl.exe -d $distro -u nicol -- wslpath -a $root).Trim()
if (-not $unixRoot) {
    Write-Error "Could not map $root into WSL"
}

$envArgs = @(
    "HOME=/home/nicol",
    "DISPLAY=:0",
    "WAYLAND_DISPLAY=wayland-0",
    "PULSE_SERVER=unix:/mnt/wslg/PulseServer",
    "XDG_RUNTIME_DIR=/mnt/wslg/runtime-dir"
)

Write-Host "Building and launching Dawn in $distro (WSLg)..."
& wsl.exe -d $distro -u nicol -- env @envArgs bash "$unixRoot/scripts/build-wsl.sh" --run
exit $LASTEXITCODE
