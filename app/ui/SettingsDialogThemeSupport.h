#pragma once

#include "ui/ThemeManager.h"

#include <QColor>
#include <QDir>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace gamecq::settings_theme {

inline QString defaultThemeFolder()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (root.isEmpty()) {
        root = QDir::homePath();
    }
    return QDir(root).filePath("GameCQ Themes");
}

inline QString safeThemeFileName(QString name)
{
    name = name.trimmed();
    if (name.isEmpty()) {
        name = "GameCQ Theme";
    }
    name.replace(QRegularExpression("[^A-Za-z0-9._-]+"), "_");
    while (name.startsWith('_')) {
        name.remove(0, 1);
    }
    while (name.endsWith('_')) {
        name.chop(1);
    }
    return name.isEmpty() ? QString("GameCQ_Theme") : name;
}

inline QString normalizedHexColor(QString color)
{
    color = color.trimmed();
    if (color.isEmpty()) {
        return {};
    }
    if (!color.startsWith('#')) {
        color.prepend('#');
    }

    static const QRegularExpression pattern("^#[0-9a-fA-F]{6}$");
    return pattern.match(color).hasMatch() ? color.toUpper() : QString();
}

inline QString scopedThemeKey(const QString &baseTheme, const QString &section, const QString &key)
{
    return QString("Theme/Presets/%1/%2/%3").arg(baseTheme, section, key);
}

inline QString scopedThemeFontKey(const QString &baseTheme)
{
    return QString("Theme/Presets/%1/FontFamily").arg(baseTheme);
}

inline bool useLegacyThemeFallback(const QString &baseTheme)
{
    return baseTheme == "dark" || baseTheme == "ember";
}

inline QString themeFontSetting(QSettings &settings, const QString &baseTheme)
{
    QString font = settings.value(scopedThemeFontKey(baseTheme)).toString().trimmed();
    if (font.isEmpty() && useLegacyThemeFallback(baseTheme)) {
        font = settings.value("Theme/FontFamily").toString().trimmed();
    }
    return font.isEmpty() ? ThemeManager::defaultFontFamily(baseTheme) : font;
}

inline int themeNumericSetting(QSettings &settings, const QString &baseTheme, const QString &key)
{
    const QString scopedKey = scopedThemeKey(baseTheme, "Sizes", key);
    if (settings.contains(scopedKey)) {
        return settings.value(scopedKey, ThemeManager::defaultNumericValue(key, baseTheme)).toInt();
    }
    if (useLegacyThemeFallback(baseTheme)) {
        return settings.value(QString("Theme/Sizes/%1").arg(key), ThemeManager::defaultNumericValue(key, baseTheme)).toInt();
    }
    return ThemeManager::defaultNumericValue(key, baseTheme);
}

inline QString themeColorSetting(QSettings &settings, const QString &baseTheme, const QString &key)
{
    QString value = settings.value(scopedThemeKey(baseTheme, "Colors", key)).toString();
    if (normalizedHexColor(value).isEmpty() && useLegacyThemeFallback(baseTheme)) {
        value = settings.value(QString("Theme/Colors/%1").arg(key)).toString();
    }
    value = normalizedHexColor(value);
    return value.isEmpty() ? ThemeManager::defaultColorValue(key, baseTheme) : value;
}

inline QLabel *makeThemeLabel(const QString &text, const QString &objectName = {})
{
    auto *label = new QLabel(text);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    return label;
}

inline QFrame *makeThemePanel(const QString &title, const QString &subtitle = {}, QWidget *parent = nullptr)
{
    auto *panel = new QFrame(parent);
    panel->setObjectName("launchInfoOverlay");
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 10, 12, 12);
    layout->setSpacing(8);

    auto *titleLabel = makeThemeLabel(title, "launchSubtitle");
    titleLabel->setStyleSheet("font-weight:800;");
    layout->addWidget(titleLabel);

    if (!subtitle.isEmpty()) {
        auto *subtitleLabel = makeThemeLabel(subtitle, "sectionLabel");
        subtitleLabel->setWordWrap(true);
        layout->addWidget(subtitleLabel);
    }

    return panel;
}

inline QString colorGroupForKey(const QString &key)
{
    if (key == "text" || key == "textStrong" || key == "textMuted" || key == "textDim") {
        return "Text";
    }
    if (key == "primary" || key == "primary2" || key == "success" || key == "warning" || key == "danger" || key == "blue"
        || key == "purple") {
        return "Accents";
    }
    return "Surfaces";
}

inline void setColorButtonPreview(QPushButton *button, const QString &color)
{
    if (!button) {
        return;
    }

    const QString normalized = normalizedHexColor(color);
    if (normalized.isEmpty()) {
        button->setStyleSheet({});
        return;
    }

    const QColor preview(normalized);
    const QColor text = preview.lightness() > 140 ? QColor("#05070a") : QColor("#ffffff");
    button->setStyleSheet(QString(
                              "QPushButton { background:%1; color:%2; border:1px solid #1c2430; }"
                              "QPushButton:hover { border-color:#ffffff; }")
                              .arg(preview.name(QColor::HexRgb), text.name(QColor::HexRgb)));
}

} // namespace gamecq::settings_theme
