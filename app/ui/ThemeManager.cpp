#include "ui/ThemeManager.h"

#include "ui/ThemeSettings.h"

#include <QApplication>

namespace ThemeManager {

QString fontFamily()
{
    return gamecq::theme_settings::fontFamilyForTheme(configuredBaseTheme());
}

QColor color(const QString &key)
{
    const QColor result(gamecq::theme_settings::colorValue(key, configuredBaseTheme()));
    return result.isValid() ? result : QColor(defaultColorValue(key));
}

QString colorName(const QString &key)
{
    return color(key).name(QColor::HexRgb);
}

int numericValue(const QString &key)
{
    return gamecq::theme_settings::numericValue(key);
}

void applyTheme(QApplication &app)
{
    app.setStyleSheet(buildStyleSheet(app.arguments()));
}

} // namespace ThemeManager

