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

Official Dawn launcher for **Windows** and **Linux**. This is a fixed release, not a rolling dev build.

The launcher installs **Destiny 2 build 86657** (Shadowkeep-era files) plus the **Dawn** overlay. It does **not** install or launch live Steam Destiny 2.

## Downloads

| Platform | Package | How to run |
| --- | --- | --- |
| Windows x64 | \`Dawn-windows-x64.zip\` | Unzip and run \`Dawn.exe\` |
| Linux x64 | \`Dawn-linux-x64.tar.gz\` | Extract, then \`chmod +x Dawn && ./Dawn\` |

Linux needs a display (WSLg or X11). No extra MinGW runtime DLLs are required on Windows.

## What's new in 0.3.2

- **Install Dawn without re-downloading Destiny 2.** If the 86657 depots are already in the folder or the local cache, the launcher only installs Dawn.
- **Granular uninstall.** Uninstall Dawn, uninstall Sunrise, or uninstall everything. Destiny 2 depot files stay unless you choose **Uninstall Full**.
- **Install button when Dawn is missing.** After removing Dawn the button shows **Install** and does not pull Dawn back on its own.
- **Status while removing mods.** The indicator shows **Removing Dawn Mod** or **Removing Sunrise Mod**.
- **Hardlink depot cache** under \`%LOCALAPPDATA%\\Dawn\\depot-cache\` so a later reinstall can bring the game files back in seconds on the same volume.
- **Reliable "done" detection** from DepotDownloader's \`depot.config\` (no more Play on pre-allocated empty files).
- **Steam login without a shipped API key.** Persona name and avatar use the public Steam profile when \`STEAM_API_KEY\` is not set.
- **Self-contained Windows build.** \`libstdc++\`, \`libgcc\`, and \`winpthread\` are statically linked so a zip from GitHub just runs.
- **Background CPU cap.** 60 FPS while focused, 30 FPS unfocused, 10 FPS while Destiny 2 is running.
- **Language picker** in the installer (English default). The game language depot is the one you choose, not the OS locale.
- **Stop / Cancel** on the split button: cancel a download that is starting, stop Destiny 2 while it is running.

## What it can do

### Install
- Steam OpenID sign-in. Destiny 2 must be on the account (anonymous download is not possible).
- Downloads content depot \`1085661\` and the selected language depot (English \`1085662\`, plus French, German, Italian, Japanese, Portuguese, Spanish, Russian, Polish, Chinese, Korean, Latam).
- App \`1085660\`, manifest pinned to build **86657**.
- First **Download** opens the install-folder settings. Files go into \`your-folder\\Dawn\`, not loose in Documents/Desktop.
- DepotDownloader **3.4.0** runs as a separate process (GPL, not linked into the launcher).
- Fetches the Dawn overlay (\`steam_api64.dll\` with product name Dawn, plus the \`Dawn\` runtime) and deploys it over the old game files.
- Skips the depot download when those files are already complete; only Dawn is installed if that is what is missing.
- Cached files are hardlinked when the cache is on the same volume. Overlay files (Dawn / Sunrise) are never written into the depot cache.

### Play
- **Play** only when the 86657 depots **and** Dawn are actually present.
- Launches \`destiny2.exe\` with \`DAWN_FOREST_BASELINE=1\` and does not write \`steam_appid.txt\`, so live Steam does not force an integrity check against the current game.
- **Stop Destiny 2** from the split button while the game is running.
- **Cancel** while the game is starting or a download is in progress.

### Uninstall
- **Only depots installed:** Uninstall (removes the game folder after saving the cache).
- **Dawn installed:** Uninstall Dawn + Uninstall Full.
- **Sunrise installed:** Uninstall Sunrise + Uninstall Full.
- **Both:** Uninstall Dawn, Uninstall Sunrise, Uninstall Full.
- Component uninstall deletes only overlay files. \`destiny2.exe\`, \`packages\`, language files, and \`.DepotDownloader\` stay.
- After Uninstall Dawn the launcher stays on **Install** until you choose to put Dawn back.

### Other
- Custom Dawn theme, English UI, no console window.
- Installer language is independent of Windows/Linux locale.
- Hot-reload of \`app_logic\` for development; packaged builds load the DLL next to the exe.
- Steam Guard / password prompt when DepotDownloader needs it. The login is remembered after the first time.

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
