#pragma once

#include <QHash>
#include <QString>

namespace gamecq::theme_styles {

QString applyThemeTokens(QString qss, const QHash<QString, QString> &replacements);
QString classicPolishOverrides(const QHash<QString, QString> &replacements);
QString vaporPolishOverrides(const QHash<QString, QString> &replacements);
QString consolePolishOverrides(const QHash<QString, QString> &replacements);
QString emeraldPolishOverrides(const QHash<QString, QString> &replacements);
QString oakPolishOverrides(const QHash<QString, QString> &replacements);
QString presetPolishOverrides(const QString &theme, const QHash<QString, QString> &replacements);

} // namespace gamecq::theme_styles
