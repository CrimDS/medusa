#include "ui/ThemePresets.h"

#include "ui/ThemeManager.h"

#include <QHash>
#include <QSettings>
#include <QStringList>
#include <QtGlobal>

namespace gamecq::theme_preset {

QString normalizedThemeId(QString themeId)
{
    themeId = themeId.trimmed().toLower();
    if (themeId == "modern" || themeId == "ember") {
        return kEmberTheme;
    }
    if (themeId == "classic" || themeId == "gcql" || themeId == "legacy" || themeId == "original") {
        return kClassicTheme;
    }
    if (themeId == "crim" || themeId == "crimson" || themeId == "crimson-gold" || themeId == "gold") {
        return kCrimsonTheme;
    }
    if (themeId == "vapor" || themeId == "vaporwave" || themeId == "synthwave" || themeId == "neon") {
        return kVaporwaveTheme;
    }
    if (themeId == "console" || themeId == "hacker" || themeId == "hacker1984" || themeId == "terminal"
        || themeId == "crt" || themeId == "green-screen") {
        return kHackerTheme;
    }
    if (themeId == "emerald" || themeId == "alien" || themeId == "xeno" || themeId == "biotech" || themeId == "hive") {
        return kAlienTheme;
    }
    if (themeId == "oak" || themeId == "palette" || themeId == "palette-preview" || themeId == "rustic"
        || themeId == "rustic-preview" || themeId == "rustic-elegance") {
        return kRusticPreviewTheme;
    }
    return kDarkTheme;
}

bool useLegacyThemeFallback(const QString &themeId)
{
    const QString theme = normalizedThemeId(themeId);
    return theme == kDarkTheme || theme == kEmberTheme;
}

QString resourcePathForTheme(const QString &themeId)
{
    return normalizedThemeId(themeId) == kEmberTheme ? QString(":/gamecq/theme_ember.qss") : QString(":/gamecq/theme.qss");
}

namespace {

QString vaporwaveColorDefault(const QString &key)
{
    static const QHash<QString, QString> colors = {
        {"windowBg", "#080315"},
        {"pageBg", "#0b0520"},
        {"panelBg", "#130a2e"},
        {"raisedBg", "#211044"},
        {"toolbarBg", "#100624"},
        {"inputBg", "#12082d"},
        {"text", "#f6eaff"},
        {"textStrong", "#ffffff"},
        {"textMuted", "#bca8df"},
        {"textDim", "#75639a"},
        {"border", "#37235f"},
        {"borderSoft", "#231642"},
        {"hover", "#2a1658"},
        {"selection", "#321a6a"},
        {"primary", "#ff4fd8"},
        {"primary2", "#35e7ff"},
        {"success", "#57ffd2"},
        {"warning", "#ffd166"},
        {"danger", "#ff5f8f"},
        {"blue", "#35e7ff"},
        {"purple", "#a66cff"},
    };
    return colors.value(key, "#f6eaff");
}

int vaporwaveNumericDefault(const QString &key)
{
    static const QHash<QString, int> values = {
        {"baseFontSize", 13},
        {"smallFontSize", 12},
        {"tinyFontSize", 11},
        {"sectionFontSize", 11},
        {"chatFontSize", 13},
        {"launchTitleFontSize", 25},
        {"heroTitleFontSize", 30},
        {"brandTitleFontSize", 23},
    };
    return values.value(key, 13);
}

QString hackerColorDefault(const QString &key)
{
    static const QHash<QString, QString> colors = {
        {"windowBg", "#000000"},
        {"pageBg", "#010401"},
        {"panelBg", "#020A04"},
        {"raisedBg", "#061707"},
        {"toolbarBg", "#020802"},
        {"inputBg", "#000000"},
        {"text", "#9CFF9C"},
        {"textStrong", "#E2FFE2"},
        {"textMuted", "#67C96B"},
        {"textDim", "#347B3C"},
        {"border", "#145A22"},
        {"borderSoft", "#0A2C12"},
        {"hover", "#0B2B12"},
        {"selection", "#124C22"},
        {"primary", "#00FF66"},
        {"primary2", "#39FF14"},
        {"success", "#00FF66"},
        {"warning", "#FFCC33"},
        {"danger", "#FF4040"},
        {"blue", "#40F7FF"},
        {"purple", "#B5FF80"},
    };
    return colors.value(key, "#9CFF9C");
}

int hackerNumericDefault(const QString &key)
{
    static const QHash<QString, int> values = {
        {"baseFontSize", 13},
        {"smallFontSize", 12},
        {"tinyFontSize", 11},
        {"sectionFontSize", 11},
        {"chatFontSize", 13},
        {"launchTitleFontSize", 23},
        {"heroTitleFontSize", 28},
        {"brandTitleFontSize", 22},
    };
    return values.value(key, 13);
}

QString alienColorDefault(const QString &key)
{
    static const QHash<QString, QString> colors = {
        {"windowBg", "#020107"},
        {"pageBg", "#05020A"},
        {"panelBg", "#090814"},
        {"raisedBg", "#102016"},
        {"toolbarBg", "#05030C"},
        {"inputBg", "#04050A"},
        {"text", "#D8FFE8"},
        {"textStrong", "#F4FFF8"},
        {"textMuted", "#86BDA2"},
        {"textDim", "#4E7567"},
        {"border", "#1A6B5C"},
        {"borderSoft", "#11362F"},
        {"hover", "#123A31"},
        {"selection", "#173F4D"},
        {"primary", "#7CFF4F"},
        {"primary2", "#00E5C8"},
        {"success", "#5CFFB1"},
        {"warning", "#F3D36A"},
        {"danger", "#FF4F8B"},
        {"blue", "#55D9FF"},
        {"purple", "#B967FF"},
    };
    return colors.value(key, "#D8FFE8");
}

int alienNumericDefault(const QString &key)
{
    static const QHash<QString, int> values = {
        {"baseFontSize", 13},
        {"smallFontSize", 12},
        {"tinyFontSize", 11},
        {"sectionFontSize", 11},
        {"chatFontSize", 13},
        {"launchTitleFontSize", 25},
        {"heroTitleFontSize", 30},
        {"brandTitleFontSize", 23},
    };
    return values.value(key, 13);
}

QString rusticPreviewColorDefault(const QString &key)
{
    static const QHash<QString, QString> colors = {
        {"windowBg", "#28222C"},
        {"pageBg", "#211C24"},
        {"panelBg", "#30282D"},
        {"raisedBg", "#4A3F40"},
        {"toolbarBg", "#28222C"},
        {"inputBg", "#1E1A20"},
        {"text", "#F4E4DC"},
        {"textStrong", "#FFF5EF"},
        {"textMuted", "#E4A390"},
        {"textDim", "#B9847A"},
        {"border", "#8A4F55"},
        {"borderSoft", "#4A3F40"},
        {"hover", "#3B3035"},
        {"selection", "#573C42"},
        {"primary", "#D49C68"},
        {"primary2", "#E4A390"},
        {"success", "#D4BA68"},
        {"warning", "#E4A390"},
        {"danger", "#C96572"},
        {"blue", "#F0BBA8"},
        {"purple", "#B16E76"},
    };
    return colors.value(key, "#F4E4DC");
}

int rusticPreviewNumericDefault(const QString &key)
{
    static const QHash<QString, int> values = {
        {"baseFontSize", 13},
        {"smallFontSize", 12},
        {"tinyFontSize", 11},
        {"sectionFontSize", 11},
        {"chatFontSize", 13},
        {"launchTitleFontSize", 24},
        {"heroTitleFontSize", 29},
        {"brandTitleFontSize", 22},
    };
    return values.value(key, 13);
}

} // namespace

} // namespace gamecq::theme_preset

namespace ThemeManager {

QStringList baseThemeIds()
{
    using namespace gamecq::theme_preset;
    return {kDarkTheme,
            kEmberTheme,
            kClassicTheme,
            kCrimsonTheme,
            kVaporwaveTheme,
            kHackerTheme,
            kAlienTheme,
            kRusticPreviewTheme};
}

QString baseThemeLabel(const QString &themeId)
{
    using namespace gamecq::theme_preset;
    const QString theme = normalizedThemeId(themeId);
    if (theme == kEmberTheme) {
        return "Ember";
    }
    if (theme == kClassicTheme) {
        return "Classic GCQL";
    }
    if (theme == kCrimsonTheme) {
        return "Crim's";
    }
    if (theme == kVaporwaveTheme) {
        return "Vapor";
    }
    if (theme == kHackerTheme) {
        return "Console";
    }
    if (theme == kAlienTheme) {
        return "Emerald";
    }
    if (theme == kRusticPreviewTheme) {
        return "Oak";
    }
    return "Dark";
}

QString normalizedBaseThemeId(const QString &themeId)
{
    return gamecq::theme_preset::normalizedThemeId(themeId);
}

QString defaultBaseTheme()
{
    return gamecq::theme_preset::kClassicTheme;
}

QString configuredBaseTheme()
{
    return gamecq::theme_preset::normalizedThemeId(QSettings().value("Theme/Base", defaultBaseTheme()).toString());
}

QString activeBaseTheme(const QStringList &arguments)
{
    QString theme = qEnvironmentVariable("GAMECQ_THEME").trimmed().toLower();
    for (int i = 1; i < arguments.size(); ++i) {
        const QString arg = arguments.at(i).trimmed();
        if (arg == "--theme" && i + 1 < arguments.size()) {
            theme = arguments.at(++i).trimmed().toLower();
        } else if (arg.startsWith("--theme=")) {
            theme = arg.mid(QString("--theme=").size()).trimmed().toLower();
        }
    }

    return theme.isEmpty() ? configuredBaseTheme() : gamecq::theme_preset::normalizedThemeId(theme);
}

QVector<ColorRole> colorRoles()
{
    return {
        {"windowBg", "Window background", "#06080c", "#100606", "#000000", "#050203"},
        {"pageBg", "Page background", "#06080c", "#100606", "#000000", "#070304"},
        {"panelBg", "Panel background", "#0a0d13", "#150909", "#000000", "#100607"},
        {"raisedBg", "Raised controls", "#11151d", "#24100d", "#2a2a2a", "#1a0b0c"},
        {"toolbarBg", "Toolbar/title bars", "#0b0f16", "#170909", "#252525", "#0c0506"},
        {"inputBg", "Input fields", "#0c0f15", "#120707", "#ffffff", "#0d0607"},
        {"text", "Body text", "#e8ebf0", "#f4dfc8", "#ffffff", "#f1e6d8"},
        {"textStrong", "Strong text", "#ffffff", "#fff0d8", "#ffffff", "#fff3df"},
        {"textMuted", "Muted text", "#9aa3b2", "#c7a996", "#d8d8d8", "#c8aa82"},
        {"textDim", "Dim text", "#5d6675", "#8d7568", "#a8a8a8", "#806653"},
        {"border", "Borders", "#1c2430", "#5f261d", "#d6d6d6", "#3a1518"},
        {"borderSoft", "Soft borders", "#161c26", "#26100d", "#777777", "#241011"},
        {"hover", "Hover/selected", "#1a212e", "#3a1712", "#333333", "#2a0d10"},
        {"selection", "Table selection", "#1a212e", "#4a1d16", "#1c1c1c", "#3a1418"},
        {"primary", "Primary accent", "#ff8a3d", "#ffae43", "#ff8a00", "#d8a542"},
        {"primary2", "Primary gradient end", "#ff5b5b", "#b84b32", "#ffb000", "#8f1423"},
        {"success", "Success/online", "#46c878", "#65d383", "#00ff00", "#51c77a"},
        {"warning", "Warning/away", "#ffb648", "#ffc46f", "#ffff00", "#d8a542"},
        {"danger", "Danger/moderation", "#ff5b5b", "#ff776b", "#ff0000", "#c22434"},
        {"blue", "Link/browser accent", "#4aa3ff", "#67a7ff", "#0099ff", "#5da8ff"},
        {"purple", "Developer accent", "#9b7cff", "#c58bff", "#ff8a00", "#b785ff"},
    };
}

QVector<NumericRole> numericRoles()
{
    return {
        {"baseFontSize", "Base font size", 13, 13, 12, 13, 9, 22},
        {"smallFontSize", "Small text", 12, 12, 11, 12, 8, 20},
        {"tinyFontSize", "Tiny/status text", 11, 11, 10, 11, 8, 18},
        {"sectionFontSize", "Section labels", 11, 11, 11, 11, 8, 18},
        {"chatFontSize", "Chat text", 13, 13, 12, 13, 9, 24},
        {"launchTitleFontSize", "Launch title", 24, 24, 20, 24, 14, 40},
        {"heroTitleFontSize", "Hero/title text", 28, 28, 24, 28, 16, 48},
        {"brandTitleFontSize", "Login brand title", 22, 22, 20, 22, 14, 40},
    };
}

QString defaultFontFamily(const QString &themeId)
{
    using namespace gamecq::theme_preset;
    const QString theme = themeId.isEmpty() ? configuredBaseTheme() : normalizedThemeId(themeId);
    if (theme == kClassicTheme) {
        return "Tahoma";
    }
    if (theme == kHackerTheme) {
        return "Consolas";
    }
    if (theme == kAlienTheme) {
        return "Bahnschrift";
    }
    if (theme == kRusticPreviewTheme) {
        return "Segoe UI";
    }
    return "Segoe UI";
}

QString defaultColorValue(const QString &key, const QString &themeId)
{
    using namespace gamecq::theme_preset;
    const QString theme = themeId.isEmpty() ? configuredBaseTheme() : normalizedThemeId(themeId);
    if (theme == kVaporwaveTheme) {
        return vaporwaveColorDefault(key);
    }
    if (theme == kHackerTheme) {
        return hackerColorDefault(key);
    }
    if (theme == kAlienTheme) {
        return alienColorDefault(key);
    }
    if (theme == kRusticPreviewTheme) {
        return rusticPreviewColorDefault(key);
    }
    for (const ColorRole &role : colorRoles()) {
        if (role.key == key) {
            if (theme == kClassicTheme) {
                return role.classicDefault;
            }
            if (theme == kCrimsonTheme) {
                return role.crimsonDefault;
            }
            return theme == kEmberTheme ? role.emberDefault : role.darkDefault;
        }
    }
    return "#e8ebf0";
}

int defaultNumericValue(const QString &key, const QString &themeId)
{
    using namespace gamecq::theme_preset;
    const QString theme = themeId.isEmpty() ? configuredBaseTheme() : normalizedThemeId(themeId);
    if (theme == kVaporwaveTheme) {
        return vaporwaveNumericDefault(key);
    }
    if (theme == kHackerTheme) {
        return hackerNumericDefault(key);
    }
    if (theme == kAlienTheme) {
        return alienNumericDefault(key);
    }
    if (theme == kRusticPreviewTheme) {
        return rusticPreviewNumericDefault(key);
    }
    for (const NumericRole &role : numericRoles()) {
        if (role.key == key) {
            if (theme == kClassicTheme) {
                return role.classicDefault;
            }
            if (theme == kCrimsonTheme) {
                return role.crimsonDefault;
            }
            return theme == kEmberTheme ? role.emberDefault : role.darkDefault;
        }
    }
    return 13;
}

} // namespace ThemeManager
