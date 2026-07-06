# Launcher And Updates

## Launch Entry Model

Each launcher item is represented by `LaunchEntry`.

Important fields:

- `id` - stable launcher ID.
- `name` - display name.
- `kind` - normalized broad type such as `game`, `software`, or `tool`.
- `category` - user-facing category.
- `source` - platform/source such as Official, Steam, Epic, GOG, or Local.
- `executable` and `workingDirectory` - process launch paths.
- `commandLine` - optional arguments; DarkSpace entries can substitute server/session data.
- `updater` and `manifestUrl` - update source.
- `needsServer` - selected server is required before launch.
- `staffOnly` - only staff profiles should see/use the entry.
- `customEntry` - user-created entry rather than built-in catalog item.

Built-in entries should stay conservative and release-safe. Experimental tools can remain visible only
to staff by setting `staffOnly`.

## Artwork

Artwork is searched in this order:

1. User launch art.
2. Bundled default launch art.
3. Program icon fallback.

Release packages include default DarkSpace art under `DefaultLaunchArt`. Users can override it through
the launcher without changing the shipped assets.

## DarkSpace D12

D12 updates use an HTTP manifest. `HttpManifestUpdater`:

1. Fetches the manifest.
2. Compares local files by path/hash/size.
3. Uses a full archive when the manifest provides one and the change set is large enough.
4. Otherwise downloads missing/changed files in bounded parallel batches.
5. Emits status/progress and then allows launch to continue.

Only Dev/Admin style staff profiles should be allowed to bypass update checks from the UI.

## DarkSpace D9

D9 uses the legacy mirror protocol rather than the D12 HTTP manifest. GameCQ builds
`GameCQLegacyUpdate.exe` as an x86 helper and copies it beside `GameCQ.exe`.

The helper uses the current lobby session ID when available and mirrors the old non-self-update
program cache behavior. It writes directly to the D9 install/cache folder instead of relying on the
old GCQL SmartUpdate replacement flow.

## Non-DarkSpace Games And Tools

User-added games/tools can launch directly and can be identified against Steam metadata. Non-DarkSpace
news/update links should open in the user's external browser. DarkSpace links use the built-in browser
because they are part of the GameCQ community workflow.

