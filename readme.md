# GameCQ 1 ⅜

GameCQ 1 ⅜ is a modern C++/Qt rewrite of the classic GameCQ/GCQL lobby client for DarkSpace.
It keeps the original community workflow intact: log into the lobby, chat with players, browse
DarkSpace community pages, install/update supported DarkSpace clients, choose a server, and launch.

The new client also acts as a program launcher. It can list user-added games and tools, gather
metadata/art/news where possible, and keep the DarkSpace D9 and D12 launch/update paths in one place.


## Repository Layout

- `app/` - Qt application source.
- `app/core/` - application paths and portable settings.
- `app/launcher/` - launch catalog, metadata, install/update, and run helpers.
- `app/net/` - MetaClient bridge/worker and network helpers.
- `app/security/` - Windows credential storage.
- `app/ui/` - windows, dialogs, chat rendering, browser, theme, and launch-page UI.
- `app/res/` - Qt resources, default QSS, app icon resources, and bundled DarkSpace launch art.
- `legacy_updater/` - x86 helper used for legacy DarkSpace D9 mirror updates.
- `libnet/` - static library wrapper around the legacy Medusa networking pieces.
- `Icons/` - classic GCQL icon assets.
- `scripts/` - build/package helper scripts.
- `docs/` - developer and release documentation.

## Build Quick Start

Requirements:

- Windows.
- CMake 3.21 or newer.
- Visual Studio with C++ support.
- Qt 6 Widgets and Network.
- Qt WebEngine Widgets if the integrated browser should be enabled.
- A local Medusa source tree for legacy lobby/mirror code.

Example configure:

```powershell
cmake -S . -B build `
  -DGAMECQ_MEDUSA_ROOT="<path-to-medusa-source>" `
  -DCMAKE_PREFIX_PATH="<path-to-qt-msvc>"
```

Replace the placeholder paths with your local Medusa source tree and Qt installation, for example
the folder that contains Qt's `lib/cmake/Qt6` configuration files.

Example build:

```powershell
cmake --build build --config Debug --target GameCQ
```

The executable is produced under `build/app/Debug/GameCQ.exe` for Debug builds.

## More Documentation

- [Known Issues](docs/known_issues.md)
- [Build And Release](docs/build_and_release.md)
- [Architecture](docs/architecture.md)
- [Source Map](docs/source_map.md)
- [Launcher And Updates](docs/launcher_and_updates.md)
- [Chat And Discord](docs/chat_and_discord.md)
- [Themes](docs/themes.md)
- [Updater Policy](docs/updater_policy.md)
