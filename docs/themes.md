# Themes

## Theme System

Themes are generated from roles. This lets the options window edit colors and sizes without maintaining
one giant QSS file per preset.

Core files:

- `ThemeManager.*` - role definitions, default values, QSS generation, chat document CSS.
- `ThemeStyleClassic.cpp` - classic GCQL-style preset.
- `ThemeStyleConsole.cpp` - 1980s terminal/hacker-inspired preset.
- `ThemeStyleEmerald.cpp` - green/alien-leaning preset.
- `ThemeStyleOak.cpp` - rustic palette preset.
- `ThemeStyleVapor.cpp` - vaporwave preset.
- `ThemeStylePolish.*` - shared polishing helpers for generated QSS.
- `ThemePresets.*` - load/save preset files.
- `ThemeSettings.*` - persisted user overrides.

The "Crim's" preset is selected through `ThemePresets` and currently gets its base values from the
shared role tables rather than a dedicated split style file.

## Editing A Preset

Prefer editing the relevant `ThemeStyle*.cpp` file. Keep preset defaults readable and only add new
roles when the existing roles cannot describe the UI element.

When adding a new role:

1. Add the role to `ThemeManager::colorRoles()` or `ThemeManager::numericRoles()`.
2. Add defaults for every base preset.
3. Use the role in QSS generation.
4. Check the options theme editor still previews the change.

## Runtime Behavior

The options dialog can apply theme changes while it is open. Saved overrides are layered over the
selected base preset and should update the whole app, including:

- main shell
- launch page
- chat log
- member lists and side drawers
- help/about dialogs
- options preview

If only part of the UI changes after Apply, check whether that widget uses generated QSS roles or has a
hard-coded style.
