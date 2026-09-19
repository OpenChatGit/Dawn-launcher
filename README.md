<p align="center">
  <img src="assets/dawn-emblem.svg" alt="Dawn" width="160" height="160">
</p>

<h1 align="center">Dawn</h1>

<p align="center">Launcher for Destiny 2 build 86657 and the Dawn overlay.</p>

This page is updated when the launcher changes. Not every bug is listed yet.

Dawn is a Windows and Linux launcher. It installs **Destiny 2 build 86657** (Shadowkeep-era files) plus the **Dawn** overlay. It does not install, patch, or launch live Steam Destiny 2.

Official packages: [Releases](https://github.com/OpenChatGit/Dawn-launcher/releases)

| Platform | Package | Run |
| --- | --- | --- |
| Windows x64 | `Dawn-windows-x64.zip` | Unzip and run `Dawn.exe` |
| Linux x64 | `Dawn-linux-x64.tar.gz` | Extract, then `chmod +x Dawn && ./Dawn` |

Linux needs a display (X11 or WSLg). Windows builds are statically linked. You do not need MinGW runtime DLLs.

The version in `VERSION` is the current official number. Dev and CI builds may append `-dev`.

## What it does

- Steam OpenID sign-in. Destiny 2 must be on the Steam account. Anonymous depot download is not possible.
- Downloads content depot `1085661` and the language depot you pick (English `1085662` by default). App `1085660`, manifest pinned to build **86657**.
- First **Download** opens the install-folder settings. Files go into `your-folder\Dawn`, not loose in Documents or Desktop.
- DepotDownloader **3.4.0** runs as a separate child process. It is GPL and is not linked into the launcher.
- Fetches the Dawn overlay (`steam_api64.dll` with product name Dawn) and deploys it over the old game files.
- Skips the depot download when those files are already complete. If only Dawn is missing, only Dawn is installed.
- Cached depot files live under `%LOCALAPPDATA%\Dawn\depot-cache` (hardlinked on the same volume). Dawn and Sunrise overlays are never written into that cache.
- **Play** only when the 86657 depots and Dawn are actually present.
- Launches `destiny2.exe` with `DAWN_FOREST_BASELINE=1` and does not write `steam_appid.txt`, so live Steam should not force an integrity check against current Destiny 2.
- Uninstall Dawn, uninstall Sunrise, or uninstall everything. Destiny 2 depot files stay unless you choose **Uninstall Full**.
- Official builds show **New Version** in the titlebar only when a newer official GitHub release exists. Dev builds (`host.exe`) always show that control so it can be tested.

## Sign in

- The account menu is Steam sign-in. Steam should show **Sign in to Dawn**. The browser callback is a Dawn page with the emblem.
- The first time that name is used, Windows may ask for admin rights so `Dawn` can resolve to this machine. If that step is skipped, Steam may still show `127.0.0.1` and the callback can fail.
- DepotDownloader password is asked only when a depot install is still needed. If the game files are already there, you only use Steam sign-in.
- That password is kept only until the depot download finishes, then it is deleted. It is not stored for later depot downloads.
- Steam Guard may still appear when DepotDownloader needs it.
- Persona name and avatar use the public Steam profile when `STEAM_API_KEY` is not set. Optional keys stay in a local `.env` and are never packed into a release.

## Updates

Official builds poll [GitHub Releases](https://github.com/OpenChatGit/Dawn-launcher/releases) for numbered tags (`vX.Y.Z`). Prerelease and draft builds are ignored.

If a newer official build exists, the titlebar shows a download icon and **New Version**. The version number is in a pill to the left. Accepting it downloads the package for this OS, replaces the launcher files, quits, and restarts. Windows cannot overwrite a running `Dawn.exe`, so a helper waits for this process to exit.

## Build from source

Windows (MSYS2 UCRT64, CMake, Ninja):

```
.\scripts\dev.ps1
```

That builds `host.exe` and `app_logic.dll` and starts the launcher. Packaged releases rename the host to `Dawn.exe`.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target host --target app_logic
```

Linux needs CMake, Ninja, g++, X11, zlib, and libcurl.

On Windows 11 with Ubuntu WSL, build and open the Linux UI on your desktop (WSLg):

```
.\scripts\dev-wsl.ps1
```

Or inside Ubuntu:

```
./scripts/build-wsl.sh --run
```

If Steam HTTP is missing in WSL:

```
sudo apt install -y libcurl4-openssl-dev
```

Pushing `main` publishes a rolling latest build. A `v*` tag publishes a fixed official release.

## Known limits

This project is early. Expect gaps.

- Do not point the install folder at a live Steam Destiny 2 library. The launcher looks for build 86657, not current Steam files.
- A depot download that was cancelled, disk-full, or interrupted may leave a folder that looks occupied. Play stays off until `depot.config` says both needed depots are finished.
- Self-update replaces launcher files only. It does not repair a broken game install.
- Dawn.localhost / `Dawn` hosts mapping is a local workaround for Steam OpenID. Some browsers or locked-down PCs may still fail the callback.
- Linux is a first-class target in CI, but most daily testing is on Windows.
- Themes, Sunrise, language depots, and cache reuse have more edge cases than the happy path.
- If something looks wrong, treat the GitHub issue list and this file as incomplete.

## License

The launcher code in this repository is what you see here. DepotDownloader is a separate tool from [SteamRE/DepotDownloader](https://github.com/SteamRE/DepotDownloader) and keeps its own license. Do not statically link it into Dawn.
