#pragma once

#include <QString>

namespace gamecq::theme_settings {

QString fontFamilyForTheme(const QString &themeId);
QString colorValue(const QString &key, const QString &themeId = {});
QString numericString(const QString &key, const QString &themeId);
int numericValue(const QString &key);
QString qssString(QString value);

} // namespace gamecq::theme_settings
