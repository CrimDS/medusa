#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>

class QApplication;

namespace ThemeManager {

// Roles are the public contract between the theme editor and generated QSS.
// Add a role only when existing roles cannot express a widget's visual state.
struct ColorRole {
    QString key;
    QString label;
    QString darkDefault;
    QString emberDefault;
    QString classicDefault;
    QString crimsonDefault;
};

struct NumericRole {
    QString key;
    QString label;
    int darkDefault = 0;
    int emberDefault = 0;
    int classicDefault = 0;
    int crimsonDefault = 0;
    int minimum = 0;
    int maximum = 0;
};

QStringList baseThemeIds();
QString baseThemeLabel(const QString &themeId);
QString normalizedBaseThemeId(const QString &themeId);
QString defaultBaseTheme();
QString configuredBaseTheme();
QString activeBaseTheme(const QStringList &arguments = {});

QVector<ColorRole> colorRoles();
QVector<NumericRole> numericRoles();

QString defaultFontFamily(const QString &themeId = {});
QString defaultColorValue(const QString &key, const QString &themeId = {});
int defaultNumericValue(const QString &key, const QString &themeId = {});

QString fontFamily();
QColor color(const QString &key);
QString colorName(const QString &key);
int numericValue(const QString &key);

QString buildStyleSheet(const QStringList &arguments = {});
QString chatDocumentStyleSheet();
void applyTheme(QApplication &app);

} // namespace ThemeManager
