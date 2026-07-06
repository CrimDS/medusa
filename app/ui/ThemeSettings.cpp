#include "ui/ThemeSettings.h"

#include "ui/ThemeManager.h"
#include "ui/ThemePresets.h"

#include <QRegularExpression>
#include <QSettings>

namespace gamecq::theme_settings {
namespace {

QString scopedThemeKey(const QString &themeId, const QString &section, const QString &key)
{
    return QString("Theme/Presets/%1/%2/%3").arg(theme_preset::normalizedThemeId(themeId), section, key);
}

QString scopedThemeFontKey(const QString &themeId)
{
    return QString("Theme/Presets/%1/FontFamily").arg(theme_preset::normalizedThemeId(themeId));
}

} // namespace

QString fontFamilyForTheme(const QString &themeId)
{
    const QString theme = themeId.isEmpty() ? ThemeManager::configuredBaseTheme() : theme_preset::normalizedThemeId(themeId);
    QSettings settings;
    QString font = settings.value(scopedThemeFontKey(theme)).toString().trimmed();
    if (font.isEmpty() && theme_preset::useLegacyThemeFallback(theme)) {
        font = settings.value("Theme/FontFamily").toString().trimmed();
    }
    return font.isEmpty() ? ThemeManager::defaultFontFamily(theme) : font;
}

QString colorValue(const QString &key, const QString &themeId)
{
    const QString theme = themeId.isEmpty() ? ThemeManager::configuredBaseTheme() : theme_preset::normalizedThemeId(themeId);
    QSettings settings;
    static const QRegularExpression hexColor("^#[0-9a-fA-F]{6}$");

    QString value = settings.value(scopedThemeKey(theme, "Colors", key)).toString().trimmed();
    if (hexColor.match(value).hasMatch()) {
        return value;
    }

    if (theme_preset::useLegacyThemeFallback(theme)) {
        value = settings.value(QString("Theme/Colors/%1").arg(key)).toString().trimmed();
    }
    return hexColor.match(value).hasMatch() ? value : ThemeManager::defaultColorValue(key, themeId);
}

QString numericString(const QString &key, const QString &themeId)
{
    const QString theme = themeId.isEmpty() ? ThemeManager::configuredBaseTheme() : theme_preset::normalizedThemeId(themeId);
    QSettings settings;
    bool ok = false;
    const QString scopedKey = scopedThemeKey(theme, "Sizes", key);
    int value = ThemeManager::defaultNumericValue(key, theme);
    if (settings.contains(scopedKey)) {
        value = settings.value(scopedKey).toInt(&ok);
    } else if (theme_preset::useLegacyThemeFallback(theme)) {
        value = settings.value(QString("Theme/Sizes/%1").arg(key), ThemeManager::defaultNumericValue(key, theme)).toInt(&ok);
    } else {
        ok = true;
    }
    return QString::number(ok ? value : ThemeManager::defaultNumericValue(key, theme));
}

int numericValue(const QString &key)
{
    QSettings settings;
    const QString theme = ThemeManager::configuredBaseTheme();
    bool ok = false;
    const QString scopedKey = scopedThemeKey(theme, "Sizes", key);
    int value = ThemeManager::defaultNumericValue(key, theme);
    if (settings.contains(scopedKey)) {
        value = settings.value(scopedKey).toInt(&ok);
    } else if (theme_preset::useLegacyThemeFallback(theme)) {
        value = settings.value(QString("Theme/Sizes/%1").arg(key), ThemeManager::defaultNumericValue(key, theme)).toInt(&ok);
    } else {
        ok = true;
    }
    if (!ok) {
        value = ThemeManager::defaultNumericValue(key, theme);
    }

    for (const ThemeManager::NumericRole &role : ThemeManager::numericRoles()) {
        if (role.key == key) {
            return qBound(role.minimum, value, role.maximum);
        }
    }
    return value;
}

QString qssString(QString value)
{
    value.replace('\\', "\\\\");
    value.replace('"', "\\\"");
    return QString("\"%1\"").arg(value);
}

} // namespace gamecq::theme_settings
