#include "ui/ThemeStylePolish.h"

#include "ui/ThemePresets.h"

#include <QStringList>

#include <algorithm>

namespace gamecq::theme_styles {

using namespace gamecq::theme_preset;

QString applyThemeTokens(QString qss, const QHash<QString, QString> &replacements)
{
    QStringList keys = replacements.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right) {
        return left.size() > right.size();
    });
    for (const QString &key : keys) {
        qss.replace(key, replacements.value(key));
    }
    return qss;
}

QString presetPolishOverrides(const QString &theme, const QHash<QString, QString> &replacements)
{
    if (theme == kClassicTheme) {
        return classicPolishOverrides(replacements);
    }
    if (theme == kVaporwaveTheme) {
        return vaporPolishOverrides(replacements);
    }
    if (theme == kHackerTheme) {
        return consolePolishOverrides(replacements);
    }
    if (theme == kAlienTheme) {
        return emeraldPolishOverrides(replacements);
    }
    if (theme == kRusticPreviewTheme) {
        return oakPolishOverrides(replacements);
    }
    return {};
}

} // namespace gamecq::theme_styles
