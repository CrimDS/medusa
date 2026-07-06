# Build And Release

## Configure

GameCQ is a CMake project. The root `CMakeLists.txt` exposes these main options:

- `GAMECQ_BUILD_APP` - builds the Qt GameCQ shell. Default: `ON`.
- `GAMECQ_BUILD_LIBNET` - builds the legacy Medusa networking wrapper. Default: `ON`.
- `GAMECQ_BUILD_LEGACY_D9_HELPER` - builds/copies the x86 legacy updater helper. Default: `ON`.
- `GAMECQ_MEDUSA_ROOT` - path to the Medusa source tree used by `libnet` and the legacy updater.

Typical local configure:

```powershell
cmake -S . -B build `
  -DGAMECQ_MEDUSA_ROOT="<path-to-medusa-source>" `
  -DCMAKE_PREFIX_PATH="<path-to-qt-msvc>"
```

Use local absolute paths for those placeholders. `GAMECQ_MEDUSA_ROOT` should point at the Medusa
source tree used by the legacy lobby/mirror libraries. `CMAKE_PREFIX_PATH` should point at the Qt
MSVC kit folder that contains Qt's `bin`, `lib`, and `lib/cmake/Qt6` directories.

Qt WebEngine is optional at configure time. If `Qt6::WebEngineWidgets` is found, GameCQ is compiled
with `GAMECQ_HAS_WEBENGINE=1` and the Browser tab uses the embedded web view. If it is not found,
browser navigation falls back to external URLs where possible.

## Build

```powershell
cmake --build build --config Debug --target GameCQ
```

For release packages:

```powershell
cmake --build build --config Release --target GameCQ
```

The D9 helper is always built as x86 because the legacy mirror/update path depends on 32-bit legacy
code. `scripts/build_legacy_updater_x86.ps1` reads the main build cache to reuse the configured
CMake generator and `GAMECQ_MEDUSA_ROOT`.

## Package

After a Release build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_gamecq_release.ps1 -Configuration Release
```

The package script copies the app payload into `dist/GameCQ`, writes `portable-settings.ini`, includes
default DarkSpace launch art, and excludes local/user/operator files.
It also runs `windeployqt` to copy the Qt runtime and WebEngine files required by the packaged browser.

The packaged folder is the release candidate. Final validation should run from `dist/GameCQ`, not from
`build/app/Release`, because portable settings, package exclusions, bundled art, and WebEngine runtime
files can only be verified from the packaged layout.

Running a packaged build can create `UserData`, `.Cache`, `DarkSpace`, `DarkSpaceD12`, and other local
state beside the executable. Those folders are useful for testing but must not be shipped. After manual
testing, rerun the package script into a fresh folder and zip that fresh output before launching it.

Excluded from packages:

- `.Cache`, `DarkSpace`, `DarkSpaceD12`, `LaunchArt`, and `UserData`.
- DiscordGhost binaries/config/personality/emote files.
- Logs and intermediate build artifacts.
- PDB files unless `-IncludeDebugSymbols` is passed.

If `windeployqt.exe` cannot be found from the CMake cache or `PATH`, pass it explicitly:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package_gamecq_release.ps1 `
  -Configuration Release `
  -WindeployQtPath "<path-to-qt-msvc>/bin/windeployqt.exe"
```

## Portable Settings

If `portable-settings.ini` exists beside `GameCQ.exe`, `AppPaths::configurePortableSettings()` stores
Qt user settings under `UserData` beside the executable. This is the desired release behavior because
GameCQ should be droppable into any folder without relying on roaming profile state.

Without `portable-settings.ini`, GameCQ uses Qt's normal per-user application data location.

## Clean Source Tree

Generated folders such as `build/` and `dist/` are not source. They can be deleted and recreated from
the configure/build/package commands above.

For the full release workflow, use [release_plan.md](release_plan.md) and
[release_checklist.md](release_checklist.md).
