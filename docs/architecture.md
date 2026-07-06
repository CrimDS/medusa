# Architecture

GameCQ is a Qt Widgets application with a thin UI shell around three larger systems:

- The legacy GameCQ MetaClient connection.
- The launcher/update/catalog system.
- The chat/browser/community UI.

The app is intentionally split by feature area rather than by classic MVC layers. Qt widgets own most
of their state directly, while network/update work is moved into workers or helper classes when it can
stall the UI.

## Startup Flow

1. `main.cpp` configures portable settings through `AppPaths`.
2. `MainWindow` creates the shell, tray icon, status line, chat, browser, and launch page.
3. `MetaClientBridge` starts a worker thread for legacy lobby networking.
4. Login is shown shortly after the event loop starts, unless auto-login is enabled.
5. Successful login selects the default DarkSpace game, joins the default room, and starts polling chat
   and membership data.

## Lobby Networking

`MetaClientBridge` is the UI-thread facade. `MetaClientWorker` owns the legacy `MetaClient` object on a
background thread and emits Qt value types back to the UI.

This keeps the old networking pump away from the main event loop. All UI updates should listen to
bridge signals rather than touching `MetaClient` directly.

## Main Window Split

`MainWindow` is declared in one header because Qt slots and member state are shared across the app, but
the implementation is split into feature files:

- `MainWindowShell.cpp` - window chrome, tabs, splitter, status/bootstrap.
- `MainWindowToolbar.cpp` - top-level toolbar controls.
- `MainWindowTray.cpp` - system tray behavior.
- `MainWindowSession.cpp` - settings, login persistence, install layout.
- `MainWindowBridgeHandlers.cpp` - MetaClient signal handlers.
- `MainWindowBrowser.cpp` - browser toolbar and navigation.
- `MainWindowChat*.cpp` - chat page, composer, log rendering, media handling.
- `MainWindowLaunch*.cpp` - launch page, actions, library, servers, updates, news, edit dialogs.
- `MainWindowSideDrawer*.cpp` - rooms/friends/fleet/staff drawers and context menus.

When adding a feature, prefer extending the matching split file over adding more code to
`MainWindow.cpp`.

## Launcher

The launcher is data-driven:

- Built-in DarkSpace entries come from `builtinLaunchCatalog()`.
- User-added entries are persisted through `customLaunchCatalog()`.
- Display names, categories, platforms, artwork lookup, install roots, and command substitution are
  centralized in `LaunchCatalog`.
- Steam/store metadata helpers live in `LaunchMetadata`.
- Actual process launching is handled by `LaunchRunner`.

DarkSpace launch entries are special because they can require a server selection and update check
before process launch.

## Themes

Themes are generated from `ThemeManager`, not hand-maintained as separate full QSS files. Presets live
in focused files such as `ThemeStyleClassic.cpp`, `ThemeStyleOak.cpp`, and `ThemeStyleVapor.cpp`.

The options dialog edits color and numeric roles. Saved overrides are layered over the selected base
theme.

## Chat Rendering

Chat messages are converted through a small rendering pipeline:

1. Legacy/GCQL chat markup is normalized in `ChatFormatting`.
2. Discord relay media is detected in `DiscordChatMedia`.
3. Renderable HTML/log lines are produced by `ChatHtmlRenderer`.
4. `MainWindowChatLog.cpp` appends and refreshes the visible log.
5. `ChatLogWriter` persists plain text logs when enabled.

The visible chat is capped to avoid long-session slowdown. Chat logs can still preserve full history.

