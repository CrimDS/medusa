# Known Issues And Deferred Work

This file tracks work intentionally deferred from the first GameCQ 1 ⅜ public test release.

## Deferred Features

- GameCQ self-update is not implemented.
- DiscordGhost is not packaged with GameCQ.
- Discord bot setup and management UI is not part of the client.
- Beta and Resourcer launch flows are placeholders/staff-only until their release plan is decided.
- Epic, GOG, Xbox, and Windows Store metadata support is not complete.
- Video previews from Discord relay are not supported.

## Operational Notes

- DarkSpace D9 uses legacy official mirror infrastructure.
- DarkSpace D12 uses the community HTTP manifest and must not use the official legacy update servers.
- GameCQ 1 ⅜ itself must not use official legacy update servers until a separate update plan exists.
- DiscordGhost and other bot tools are run locally by staff/operators and should stay outside release packages.

## Areas To Watch During Testing

- Browser startup, especially white/blank page delays in packaged builds.
- Options open/apply/close latency.
- Theme application on the launcher library and chat log.
- Long chat sessions with Discord relay media enabled.
- D9 update helper behavior on machines without developer tools.
- Non-staff visibility of staff rooms, staff list, staff servers, and staff-only launch entries.

## Release Blocker Template

When a tester reports a blocker, capture:

- GameCQ build/date.
- Windows version.
- Account type used.
- Packaged build or dev build.
- Steps to reproduce.
- Expected behavior.
- Actual behavior.
- Screenshot or error text.
- Whether restarting GameCQ changes the behavior.
