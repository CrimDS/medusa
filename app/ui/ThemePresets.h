#pragma once

#include <QString>

namespace gamecq::theme_preset {

inline constexpr auto kDarkTheme = "dark";
inline constexpr auto kEmberTheme = "ember";
inline constexpr auto kClassicTheme = "classic";
inline constexpr auto kCrimsonTheme = "crimson";
inline constexpr auto kVaporwaveTheme = "vaporwave";
inline constexpr auto kHackerTheme = "hacker1984";
inline constexpr auto kAlienTheme = "alien";
inline constexpr auto kRusticPreviewTheme = "rustic-preview";

QString normalizedThemeId(QString themeId);
bool useLegacyThemeFallback(const QString &themeId);
QString resourcePathForTheme(const QString &themeId);

} // namespace gamecq::theme_preset
