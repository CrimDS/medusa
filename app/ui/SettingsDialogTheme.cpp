#include "ui/SettingsDialog.h"

#include "ui/SettingsDialogThemeSupport.h"
#include "ui/ThemeManager.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFontComboBox>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

using namespace gamecq::settings_theme;

void SettingsDialog::loadThemeSettings()
{
    QSettings settings;
    const QString baseTheme = ThemeManager::normalizedBaseThemeId(settings.value("Theme/Base", ThemeManager::defaultBaseTheme()).toString());
    const int themeIndex = m_themeBase->findData(baseTheme);
    m_themeBase->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);

    const QString normalizedBase = m_themeBase->currentData().toString();
    m_themeFont->setCurrentFont(QFont(themeFontSetting(settings, normalizedBase)));

    for (const ThemeManager::NumericRole &role : ThemeManager::numericRoles()) {
        if (QSpinBox *spin = m_themeSizeControls.value(role.key, nullptr)) {
            spin->setValue(themeNumericSetting(settings, normalizedBase, role.key));
        }
    }

    for (ThemeColorControl &control : m_themeColorControls) {
        const QString value = themeColorSetting(settings, normalizedBase, control.key);
        control.field->setText(value.toUpper());
        setColorButtonPreview(control.button, value);
    }

    updateThemePreview();
}

void SettingsDialog::chooseThemeColor()
{
    auto *button = qobject_cast<QPushButton *>(sender());
    if (!button) {
        return;
    }

    const QString key = button->property("themeColorKey").toString();
    for (ThemeColorControl &control : m_themeColorControls) {
        if (control.key != key) {
            continue;
        }

        const QString baseTheme = m_themeBase->currentData().toString();
        const QString current = normalizedThemeColor(control.field->text());
        const QColor initial(current.isEmpty() ? ThemeManager::defaultColorValue(key, baseTheme) : current);
        const QColor color = QColorDialog::getColor(initial, this, "Theme Color");
        if (color.isValid()) {
            control.field->setText(color.name(QColor::HexRgb).toUpper());
        }
        return;
    }
}

void SettingsDialog::resetThemeEditor()
{
    const QString baseTheme = m_themeBase->currentData().toString();
    m_themeFont->setCurrentFont(QFont(ThemeManager::defaultFontFamily(baseTheme)));

    for (const ThemeManager::NumericRole &role : ThemeManager::numericRoles()) {
        if (QSpinBox *spin = m_themeSizeControls.value(role.key, nullptr)) {
            spin->setValue(ThemeManager::defaultNumericValue(role.key, baseTheme));
        }
    }

    for (ThemeColorControl &control : m_themeColorControls) {
        const QString value = ThemeManager::defaultColorValue(control.key, baseTheme).toUpper();
        control.field->setText(value);
        setColorButtonPreview(control.button, value);
    }

    updateThemePreview();
}

void SettingsDialog::saveThemeToFile()
{
    QJsonObject root;
    root.insert("format", "GameCQTheme");
    root.insert("version", 1);
    root.insert("name", m_themeBase->currentText());
    root.insert("basePreset", m_themeBase->currentData().toString());
    root.insert("basePresetLabel", m_themeBase->currentText());
    root.insert("fontFamily", m_themeFont->currentFont().family());

    QJsonObject sizes;
    for (const ThemeManager::NumericRole &role : ThemeManager::numericRoles()) {
        if (QSpinBox *spin = m_themeSizeControls.value(role.key, nullptr)) {
            sizes.insert(role.key, spin->value());
        }
    }
    root.insert("sizes", sizes);

    QJsonObject colors;
    for (const ThemeColorControl &control : m_themeColorControls) {
        const QString color = normalizedThemeColor(control.field->text());
        if (color.isEmpty()) {
            control.field->setFocus();
            QMessageBox::warning(this, "Save Theme", "Theme colors must use #RRGGBB hex values before saving.");
            return;
        }
        colors.insert(control.key, color);
    }
    root.insert("colors", colors);

    QDir().mkpath(defaultThemeFolder());
    QString path = QFileDialog::getSaveFileName(
        this,
        "Save Theme",
        QDir(defaultThemeFolder()).filePath(QString("%1.gcqtheme").arg(safeThemeFileName(m_themeBase->currentText()))),
        "GameCQ Theme (*.gcqtheme);;JSON (*.json);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(".gcqtheme", Qt::CaseInsensitive) && !path.endsWith(".json", Qt::CaseInsensitive)) {
        path += ".gcqtheme";
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, "Save Theme", QString("Could not write theme file:\n%1").arg(QDir::toNativeSeparators(path)));
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    QMessageBox::information(this, "Save Theme", QString("Theme saved:\n%1").arg(QDir::toNativeSeparators(path)));
}

void SettingsDialog::loadThemeFromFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Load Theme",
        defaultThemeFolder(),
        "GameCQ Theme (*.gcqtheme);;JSON (*.json);;All Files (*)");
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Load Theme", QString("Could not read theme file:\n%1").arg(QDir::toNativeSeparators(path)));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QMessageBox::warning(this, "Load Theme", QString("Theme file is not valid JSON:\n%1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject root = document.object();
    if (root.value("format").toString() != "GameCQTheme" && !root.contains("colors") && !root.contains("sizes")) {
        QMessageBox::warning(this, "Load Theme", "This does not look like a GameCQ theme file.");
        return;
    }

    const QString importedBase = ThemeManager::normalizedBaseThemeId(root.value("basePreset").toString());
    const int themeIndex = m_themeBase->findData(importedBase);
    if (themeIndex >= 0) {
        if (m_themeBase->currentIndex() == themeIndex) {
            resetThemeEditor();
        } else {
            m_themeBase->setCurrentIndex(themeIndex);
        }
    } else if (!root.value("basePreset").toString().trimmed().isEmpty()) {
        QMessageBox::information(this, "Load Theme", "The theme's base preset was not recognized. The current preset will be used.");
    }

    const QString fontFamily = root.value("fontFamily").toString().trimmed();
    if (!fontFamily.isEmpty()) {
        m_themeFont->setCurrentFont(QFont(fontFamily));
    }

    const QJsonObject sizes = root.value("sizes").toObject();
    for (const ThemeManager::NumericRole &role : ThemeManager::numericRoles()) {
        QSpinBox *spin = m_themeSizeControls.value(role.key, nullptr);
        const QJsonValue value = sizes.value(role.key);
        if (spin && value.isDouble()) {
            spin->setValue(qBound(role.minimum, value.toInt(), role.maximum));
        }
    }

    const QJsonObject colors = root.value("colors").toObject();
    for (ThemeColorControl &control : m_themeColorControls) {
        const QString color = normalizedThemeColor(colors.value(control.key).toString());
        if (!color.isEmpty()) {
            control.field->setText(color);
            setColorButtonPreview(control.button, color);
        }
    }

    updateThemePreview();
    QMessageBox::information(this, "Load Theme", "Theme loaded into the editor. Press OK to apply it.");
}

void SettingsDialog::updateThemePreview()
{
    if (!m_themePreview) {
        return;
    }

    const QString baseTheme = m_themeBase ? m_themeBase->currentData().toString() : ThemeManager::defaultBaseTheme();
    auto colorValue = [&](const QString &key) {
        for (const ThemeColorControl &control : m_themeColorControls) {
            if (control.key == key) {
                const QString normalized = normalizedThemeColor(control.field->text());
                if (!normalized.isEmpty()) {
                    return normalized;
                }
            }
        }
        return ThemeManager::defaultColorValue(key, baseTheme);
    };
    auto numericValue = [&](const QString &key) {
        if (QSpinBox *spin = m_themeSizeControls.value(key, nullptr)) {
            return spin->value();
        }
        return ThemeManager::defaultNumericValue(key, baseTheme);
    };

    const QString windowBg = colorValue("windowBg");
    const QString panelBg = colorValue("panelBg");
    const QString raisedBg = colorValue("raisedBg");
    const QString text = colorValue("text");
    const QString textStrong = colorValue("textStrong");
    const QString textMuted = colorValue("textMuted");
    const QString border = colorValue("border");
    const QString borderSoft = colorValue("borderSoft");
    const QString primary = colorValue("primary");
    const QString success = colorValue("success");
    const QString blue = colorValue("blue");

    QFont previewFont = m_themeFont ? m_themeFont->currentFont() : QFont(ThemeManager::defaultFontFamily(baseTheme));
    m_themePreview->setFont(previewFont);
    m_themePreview->setStyleSheet(QString(
                                      "QFrame#themePreviewSurface { background:%1; border:1px solid %2; border-radius:8px; }"
                                      "QLabel#themePreviewTitle { color:%3; font-size:%4px; font-weight:800; background:transparent; }"
                                      "QLabel#themePreviewMuted { color:%5; font-size:%6px; background:transparent; }"
                                      "QLabel#themePreviewChat { background:%7; color:%8; border:1px solid %9; border-radius:6px; padding:8px; font-size:%10px; }"
                                      "QLabel#themePreviewStatus { color:%11; font-weight:700; background:transparent; }"
                                      "QLabel#themePreviewAccent { color:%12; background:transparent; }"
                                      "QPushButton { background:%13; color:%12; border:1px solid %12; border-radius:6px; padding:6px 10px; font-weight:700; }")
                                      .arg(windowBg,
                                          border,
                                          textStrong,
                                          QString::number(numericValue("launchTitleFontSize")),
                                          textMuted,
                                          QString::number(numericValue("smallFontSize")),
                                          panelBg,
                                          text,
                                          borderSoft,
                                          QString::number(numericValue("chatFontSize")),
                                          success,
                                          primary,
                                          raisedBg));
    if (m_themePreviewAccent) {
        m_themePreviewAccent->setText(QString("Primary accent / link sample: %1").arg(blue));
    }
}

bool SettingsDialog::themeSettingsChanged(QSettings &settings) const
{
    const QString baseTheme = m_themeBase->currentData().toString();
    const QString savedBaseTheme = ThemeManager::normalizedBaseThemeId(settings.value("Theme/Base", ThemeManager::defaultBaseTheme()).toString());
    if (baseTheme != savedBaseTheme) {
        return true;
    }

    const QString fontFamily = m_themeFont->currentFont().family();
    if (fontFamily.compare(themeFontSetting(settings, baseTheme), Qt::CaseInsensitive) != 0) {
        return true;
    }

    for (const ThemeManager::NumericRole &role : ThemeManager::numericRoles()) {
        const QSpinBox *spin = m_themeSizeControls.value(role.key, nullptr);
        if (spin && spin->value() != themeNumericSetting(settings, baseTheme, role.key)) {
            return true;
        }
    }

    for (const ThemeColorControl &control : m_themeColorControls) {
        const QString color = normalizedThemeColor(control.field->text());
        if (color.isEmpty()) {
            return true;
        }
        if (color.compare(themeColorSetting(settings, baseTheme, control.key), Qt::CaseInsensitive) != 0) {
            return true;
        }
    }

    return false;
}

bool SettingsDialog::saveThemeSettings(QSettings &settings)
{
    const QString baseTheme = m_themeBase->currentData().toString();
    settings.setValue("Theme/Base", baseTheme);

    const QString fontFamily = m_themeFont->currentFont().family();
    settings.remove("Theme/FontFamily");
    if (fontFamily.compare(ThemeManager::defaultFontFamily(baseTheme), Qt::CaseInsensitive) == 0) {
        // Store only overrides. This keeps theme files/settings small and lets
        // preset defaults evolve without stale copied values masking them.
        settings.remove(scopedThemeFontKey(baseTheme));
    } else {
        settings.setValue(scopedThemeFontKey(baseTheme), fontFamily);
    }

    for (const ThemeManager::NumericRole &role : ThemeManager::numericRoles()) {
        QSpinBox *spin = m_themeSizeControls.value(role.key, nullptr);
        if (!spin) {
            continue;
        }

        const QString legacyKey = QString("Theme/Sizes/%1").arg(role.key);
        const QString key = scopedThemeKey(baseTheme, "Sizes", role.key);
        settings.remove(legacyKey);
        if (spin->value() == ThemeManager::defaultNumericValue(role.key, baseTheme)) {
            settings.remove(key);
        } else {
            settings.setValue(key, spin->value());
        }
    }

    for (const ThemeColorControl &control : m_themeColorControls) {
        const QString color = normalizedThemeColor(control.field->text());
        if (color.isEmpty()) {
            control.field->setFocus();
            QMessageBox::warning(this, "Theme", "Theme colors must use #RRGGBB hex values.");
            return false;
        }

        const QString legacyKey = QString("Theme/Colors/%1").arg(control.key);
        const QString key = scopedThemeKey(baseTheme, "Colors", control.key);
        settings.remove(legacyKey);
        if (color.compare(ThemeManager::defaultColorValue(control.key, baseTheme), Qt::CaseInsensitive) == 0) {
            settings.remove(key);
        } else {
            settings.setValue(key, color);
        }
    }

    return true;
}

void SettingsDialog::applyThemeSettings()
{
    if (auto *button = qobject_cast<QPushButton *>(sender())) {
        // Applying a theme used to leave the Apply button visually pressed on
        // some styles. Clear the active button state before repainting the app.
        button->clearFocus();
        button->setDown(false);
    }
    setFocus(Qt::OtherFocusReason);

    QSettings settings;
    if (!saveThemeSettings(settings)) {
        return;
    }
    emit themeApplied();
}


