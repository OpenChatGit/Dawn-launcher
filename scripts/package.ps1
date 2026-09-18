$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"
$linuxBuild = Join-Path $root "build-linux"
$dist = Join-Path $root "dist\Dawn"
$zip = Join-Path $root "dist\Dawn-Launcher.zip"

function Copy-IfExists([string] $From, [string] $To) {
    if (Test-Path -LiteralPath $From) {
        $parent = Split-Path -Parent $To
        if ($parent) {
            New-Item -ItemType Directory -Force -Path $parent | Out-Null
        }
        Copy-Item -LiteralPath $From -Destination $To -Force
        return $true
    }
    return $false
}

if (-not (Test-Path (Join-Path $build "host.exe"))) {
    Write-Error "build/host.exe is missing. Build the Windows launcher first."
}

Remove-Item -LiteralPath $dist -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $dist | Out-Null

Copy-IfExists (Join-Path $build "host.exe") (Join-Path $dist "Dawn.exe") | Out-Null
Copy-IfExists (Join-Path $build "app_logic.dll") (Join-Path $dist "app_logic.dll") | Out-Null
Copy-Item -LiteralPath (Join-Path $root "themes") -Destination (Join-Path $dist "themes") -Recurse -Force

$toolsSrc = Join-Path $root "tools"
if (Test-Path $toolsSrc) {
    Copy-Item -LiteralPath $toolsSrc -Destination (Join-Path $dist "tools") -Recurse -Force
}

$linuxHost = Join-Path $linuxBuild "host"
if (Test-Path -LiteralPath $linuxHost) {
    New-Item -ItemType Directory -Force -Path (Join-Path $dist "linux") | Out-Null
    Copy-IfExists $linuxHost (Join-Path $dist "linux\Dawn") | Out-Null
    Copy-IfExists (Join-Path $linuxBuild "app_logic.so") (Join-Path $dist "app_logic.so") | Out-Null
}

Copy-Item -LiteralPath (Join-Path $root "packaging\Dawn") -Destination (Join-Path $dist "Dawn") -Force

Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $dist "*") -DestinationPath $zip -Force

Write-Host "Packed $zip"
Write-Host "Windows: run Dawn.exe"
Write-Host "Linux:   chmod +x Dawn && ./Dawn"
