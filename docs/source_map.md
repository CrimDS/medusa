# Source Map

This is the quick "where do I change this?" guide.

## App Core

- `app/main.cpp` - application entry point and startup theme/settings hook.
- `app/core/AppPaths.*` - portable mode, local data paths, and Qt settings location.
- `app/security/CredentialStore.*` - saved password storage through Windows credential APIs.

## Networking

- `app/net/MetaClientBridge.*` - public Qt-facing lobby API.
- `app/net/MetaClientWorker.*` - background-thread owner of the legacy `MetaClient`.
- `app/net/MetaClientTypes.h` - Qt-friendly room/member/server/message structs.
- `app/net/GameProtocolConstants.h` - DarkSpace/GCQL protocol constants.
- `app/net/ProfileFlags.h` - helper functions for MOD/DEV/FREE/subscribed/etc. flags.
- `app/net/HttpFetch.*` - small synchronous HTTP fetch helper used by metadata routines.

## Launcher

- `app/launcher/LaunchCatalog.*` - built-in/custom entries, path resolution, artwork lookup, sorting metadata.
- `app/launcher/LaunchEntryDialog.*` - add/edit game or software dialog.
- `app/launcher/LaunchMetadata.*` - Steam metadata/news/server probe and DarkSpace update parsing.
- `app/launcher/LaunchRunner.*` - starts external programs with substituted arguments.
- `app/launcher/HttpManifestUpdater.*` - D12 HTTP manifest updater.
- `app/launcher/LegacyUpdateRunner.*` - wrapper around the x86 D9 helper.

## Main Window Features

- `app/ui/MainWindow.cpp` - constructor only; do not grow this unless adding top-level services.
- `app/ui/MainWindowShell.cpp` - shell, tabs, splitter, menus, status bar, close/native events.
- `app/ui/MainWindowTray.cpp` - tray hide/show/exit and launch submenus.
- `app/ui/MainWindowSession.cpp` - settings load/save, login persistence, install folder setup.
- `app/ui/MainWindowBridgeHandlers.cpp` - lobby event handlers.
- `app/ui/MainWindowBrowser.cpp` - browser controls and navigation.
- `app/ui/MainWindowChat.cpp` - chat page construction.
- `app/ui/MainWindowChatComposer.cpp` - input bar, color button, send behavior.
- `app/ui/MainWindowChatLog.cpp` - visible chat rendering, log theme refresh, media requests.
- `app/ui/MainWindowLaunchPage.cpp` - launch page layout.
- `app/ui/MainWindowLaunchLibrary.cpp` - library list, filters, sorting, drag ordering.
- `app/ui/MainWindowLaunchDetails.cpp` - selected item feature panel.
- `app/ui/MainWindowLaunchActions.cpp` - launch page buttons and context menus.
- `app/ui/MainWindowLaunchServers.cpp` - DarkSpace server cards and expansion behavior.
- `app/ui/MainWindowLaunchUpdates.cpp` - DarkSpace development log display.
- `app/ui/MainWindowLaunchNews.cpp` - non-DarkSpace news/update display.
- `app/ui/MainWindowLaunchRun.cpp` - update-before-launch and process start flow.
- `app/ui/MainWindowLaunchEdit.cpp` - add/edit/delete/artwork support.
- `app/ui/MainWindowSideDrawer*.cpp` - rooms/friends/fleet/staff drawers and user menus.

## Dialogs And UI Helpers

- `app/ui/LoginDialog.*` - login screen.
- `app/ui/SettingsDialog*.cpp` - options tabs, theme editor, emotes editor.
- `app/ui/HelpDialogs.*` - About and Chat Commands windows.
- `app/ui/UserDialogs.*` - find user, ignore user, friends, profile/action dialogs.
- `app/ui/WindowChrome.*` - frameless resize/chrome support.
- `app/ui/StatusLine.*` - bottom login/status/online line.
- `app/ui/LegacyIcons.*` - classic GCQL icon lookup.

## Chat Helpers

- `app/ui/ChatFormatting.*` - legacy markup, HTML entity, outgoing color normalization.
- `app/ui/ChatFormattingMarkup.cpp` - parser details for legacy rich-text markup.
- `app/ui/DiscordChatMedia.*` - Discord relay media detection/filter categorization.
- `app/ui/ChatHtmlRenderer.*` - HTML/log rendering.
- `app/ui/ChatMediaCache.*` - downloaded media cache.
- `app/ui/ChatLogWriter.*` - local chat log files.
- `app/ui/EmoteStore.*` - user/default emote persistence.

## Themes

- `app/ui/ThemeManager.*` - theme role definitions and QSS generation.
- `app/ui/ThemePresets.*` - preset import/export helpers.
- `app/ui/ThemeSettings.*` - saved theme override helpers.
- `app/ui/ThemeStyle*.cpp` - individual preset palettes and polish.

