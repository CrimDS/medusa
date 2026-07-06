# Chat And Discord

## Lobby Chat

The lobby chat path starts in `MetaClientWorker::drainChat()` and flows through `MainWindow` bridge
handlers into the chat renderer.

Visible rendering and file logging are intentionally separate:

- The chat pane renders HTML.
- Local chat logs write plain text.
- The visible chat keeps a capped number of rendered lines to avoid long-session slowdown.

## Local Chat Logs And Historical Scrollback

Chat logging writes daily plain-text logs under the configured chat log folder. In portable release
builds, the default location resolves under `UserData/GameCQ/ChatLogs` beside the executable.

Users can enable `Load previous chat log on login` in Options > Chat. When enabled, GameCQ scans the
configured log folder for correctly named logs belonging to the current account, loads the most
recent lines across those files after login, inserts visible history and session dividers, and renders
those restored lines in the chat pane. New logs use the session-scoped format
`GameCQ_<account>_yyyy-MM-dd_HH-mm-ss_<session-id>.log`; older daily logs using
`GameCQ_<account>_yyyy-MM-dd.log` are still recognized. Restored history is display-only: it is not
treated as live lobby traffic and is not written back into the log file.

## Legacy Markup

GameCQ supports classic GCQL-style markup and server/bot text:

- `[b]`, `[B]`, `[i]`, `[u]`.
- `[color=ffcc55]...[/color]`.
- `[url]...[/url]` and `[url=http://...]text[/url]`.
- Legacy server fragments such as `<b>`, `<i>`, and `<font color=...>`.
- HTML entities such as `&quot;`, `&acute;`, `&trade;`, `&hearts;`, and numeric entities.
- Raw `http://`, `https://`, `ftp://`, and `www.` links.
- Stray closing tags from old help/bot text are swallowed where possible.

Outgoing color normalization is handled by `legacyOutgoingChatColor()` and
`normalizedOutgoingChatText()`.

## Discord Relay Filtering

Discord relay messages are categorized by `DiscordRelayContent`:

- `Text`
- `Image`
- `Gif`

Chat options can suppress entire Discord relay categories, not just previews. For example, a user can
show text-only relay messages while hiding image/GIF relay messages entirely.

## Media Previews

Discord image/sticker/GIF previews are detected from trusted Discord media hosts and known relay text
formats. The current goal is inline chat presentation that blends with the chat log, not a separate
media dock.

Video preview support is intentionally not part of the current client.

## Operator Bots

DiscordGhost, LobbyGhost, and other community/operator bots are separate tools. They may bridge through
GameCQ chat, but their personalities, emote scripts, and runtime config should not be packaged with the
client.
