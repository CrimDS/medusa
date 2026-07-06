# GameCQ Updater Policy


## Current update sources

- GameCQ 1 ⅜ client: no self-updater is wired yet. Will be handled after decisions are made.

- DarkSpace D12 x64: uses Jack's  manifest at `https://jack-online.co.uk/ds/dist/manifest.json`. Large installs use the manifest's full update archive when available; smaller patches use bounded parallel per-file downloads.
- 
- DarkSpace D9 x86: the live D9 client uses `mirror-server.palestar.com:9101` from `CacheConfig.xml`; GCQL does the download through its embedded `SelfUpdate/ClientUpdate` + `Network/MirrorClient` code path with the current lobby session ID.

## Legacy SmartUpdate notes

The installed live GCQL does not ship a standalone MirrorClient executable. It ships `SelfUpdate.dll`, `Network.dll`, `SmartUpdate.exe`, and `SmartUpdate2.exe`.

`SmartUpdate` is the second-stage file applier/relauncher for GCQL self-updates. `ClientUpdate::updateSelf()` first downloads changed files through the mirror protocol, writes `Update.ini`, copies `SmartUpdate.exe` to `SmartUpdate2.exe`, then exits so `SmartUpdate2.exe` can replace locked files and relaunch GCQL.

DarkSpace D9 program installs are different: old `CacheList::startUpdate()` creates a hidden `ClientUpdate` for the program cache with `bSelfUpdating=false`, so files are written directly into `.Cache/DarkSpace`. SmartUpdate alone cannot install D9 because it only applies already-downloaded `.upd` files and install steps.

GameCQ 1 ⅜ builds and ships `GameCQLegacyUpdate.exe` as a small x86 helper for D9 installs. It mirrors old `ClientUpdate::doUpdate()` behavior: open the D9 mirror, log in with the lobby session ID when available, synchronize with `bSelfUpdating=false`, and write files directly into `.Cache/DarkSpace`.

The helper uses the mirror protocol's batch file request (`SERVER_SEND_FILES`) for fresh installs and repairs. It groups downloads into batches of up to 8192 files or 1 GB, with a single-file fallback if a batch fails, which avoids thousands of per-file mirror round trips. Even Debug GameCQ builds copy a Release x86 helper beside `GameCQ.exe`, since the D9 updater has to behave like a shipping downloader rather than a debug transport test.

The old x86 `MirrorClientD.exe` is only a development fallback. It cannot accept a lobby session ID and should not be the shipped path.

## Install layout

Portable installs should be rooted beside `GameCQ.exe` where possible:

- `.Cache`
- `.Cache/DarkSpace`
- `DarkSpaceD12`

The D12 updater should keep comparing the manifest against local files and downloading only missing or changed files.
