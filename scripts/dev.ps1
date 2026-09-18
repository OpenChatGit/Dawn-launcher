$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"

$extra = @(
    "C:\msys64\ucrt64\bin",
    "C:\Program Files\CMake\bin"
)
foreach ($dir in $extra) {
    if (Test-Path $dir) {
        $env:Path = "$dir;$env:Path"
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "cmake not found on PATH"
}

New-Item -ItemType Directory -Force -Path $build | Out-Null

Push-Location $root
try {
    if (-not (Test-Path (Join-Path $build "CMakeCache.txt"))) {
        cmake -S $root -B $build -G Ninja
    }
    cmake --build $build --target host --target app_logic
    Start-Process -FilePath (Join-Path $build "host.exe") -WorkingDirectory $build
}
finally {
    Pop-Location
}
