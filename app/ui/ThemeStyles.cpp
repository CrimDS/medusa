#include "ui/ThemeManager.h"

#include "ui/ThemePresets.h"
#include "ui/ThemeSettings.h"

#include <QColor>
#include <QFile>
#include <QHash>
#include "ui/ThemeStylePolish.h"
#include <QStringList>

#include <algorithm>

namespace {

using namespace gamecq::theme_preset;
using gamecq::theme_settings::colorValue;
using gamecq::theme_settings::fontFamilyForTheme;
using gamecq::theme_settings::numericString;

QString readResourceText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString rgba(const QString &colorText, int alpha)
{
    QColor color(colorText);
    if (!color.isValid()) {
        color = QColor("#000000");
    }
    return QString("rgba(%1, %2, %3, %4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(alpha);
}

QString themeOverrides(const QString &themeId)
{
    const QString theme = normalizedThemeId(themeId);
    const QString windowBg = colorValue("windowBg", themeId);
    const QString pageBg = colorValue("pageBg", themeId);
    const QString panelBg = colorValue("panelBg", themeId);
    const QString raisedBg = colorValue("raisedBg", themeId);
    const QString toolbarBg = colorValue("toolbarBg", themeId);
    const QString inputBg = colorValue("inputBg", themeId);
    const QString text = colorValue("text", themeId);
    const QString textStrong = colorValue("textStrong", themeId);
    const QString textMuted = colorValue("textMuted", themeId);
    const QString textDim = colorValue("textDim", themeId);
    const QString border = colorValue("border", themeId);
    const QString borderSoft = colorValue("borderSoft", themeId);
    const QString hover = colorValue("hover", themeId);
    const QString selection = colorValue("selection", themeId);
    const QString primary = colorValue("primary", themeId);
    const QString primary2 = colorValue("primary2", themeId);
    const QString success = colorValue("success", themeId);
    const QString warning = colorValue("warning", themeId);
    const QString danger = colorValue("danger", themeId);
    const QString blue = colorValue("blue", themeId);
    const QString purple = colorValue("purple", themeId);

    QString qss = R"(

/* User theme overrides */
* {
    font-family: @fontFamily, Tahoma, sans-serif;
    font-size: @baseFontSizepx;
}

QMainWindow,
QWidget,
QStackedWidget#topWorkspace,
QWidget#persistentLobby,
QFrame#pageBody,
QScrollArea#launchScrollArea,
QScrollArea#launchScrollArea > QWidget,
QScrollArea#launchScrollArea > QWidget > QWidget {
    background: @pageBg;
    color: @text;
}

QFrame#windowChrome,
QMenuBar,
QStatusBar {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @toolbarBg, stop:1 @windowBg);
    color: @textMuted;
    border-color: @border;
}

QMenuBar#mainMenuBar,
QWidget#windowChromeLeft,
QWidget#windowControls,
QFrame#windowChrome QLabel,
QLabel#windowAppIcon,
QFrame#membersHeader QLabel,
QFrame#membersActions QLabel,
QFrame#sideDrawerHeader QLabel,
QFrame#membersActions QToolButton,
QFrame#sideDrawerHeader QToolButton,
QWidget#statusLineContainer,
QWidget#bottomStatusLine,
QWidget#bottomStatusLine QLabel,
#bottomStatusLine,
#bottomStatusLine QLabel,
StatusLine,
StatusLine QLabel,
QWidget#pillCell,
QFrame#launchLibraryPanel QLabel,
QFrame#launchDetailPanel QLabel,
QFrame#launchInfoOverlay QLabel,
QFrame#launchActionsOverlay QLabel,
QFrame#launchServersOverlay QLabel {
    background: transparent;
    border: 0;
}

QMenuBar::item:selected,
QToolButton:hover,
QToolButton:pressed,
QToolButton:checked,
QPushButton:hover,
QListWidget::item:hover,
QListWidget::item:selected,
QTableWidget::item:selected {
    background: @hover;
    color: @textStrong;
}

QMenu {
    background: @panelBg;
    background-color: @panelBg;
    color: @text;
    border: 1px solid @border;
}

QMenu::item:selected {
    background: @hover;
}

QTabBar#mainTabs,
QFrame#contextToolbar,
QFrame#composer {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @toolbarBg, stop:1 @windowBg);
    border-color: @border;
}

QTabBar#mainTabs::tab {
    color: @textMuted;
}

QTabBar#mainTabs::tab:hover {
    background: @panelBg;
    color: @text;
}

QTabBar#mainTabs::tab:selected {
    background: @raisedBg;
    border-color: @border;
    color: @textStrong;
}

QToolButton,
QPushButton {
    background: @raisedBg;
    color: @text;
    border: 1px solid @border;
}

QToolButton:hover,
QToolButton:pressed,
QToolButton:checked,
QPushButton:hover {
    border-color: @primary;
}

QLineEdit,
QTextEdit,
QPlainTextEdit,
QComboBox,
QSpinBox {
    background: @inputBg;
    color: @text;
    border: 1px solid @border;
    selection-background-color: @primary;
}

QLineEdit:focus,
QTextEdit:focus,
QPlainTextEdit:focus,
QComboBox:focus,
QSpinBox:focus {
    border-color: @primary;
}

QFrame#browserAddressFrame {
    background: @inputBg;
    border: 1px solid @border;
    border-radius: 7px;
}

QLineEdit#browserAddress,
QLineEdit#browserAddress:focus {
    background: transparent;
    border: 0;
    border-radius: 0;
    padding: 5px 5px 5px 9px;
}

QToolButton#browserAddressClear {
    background: transparent;
    border: 0;
    border-radius: 3px;
    color: @textMuted;
    min-width: 18px;
    max-width: 18px;
    min-height: 18px;
    max-height: 18px;
    padding: 0;
    margin: 0;
}

QToolButton#browserAddressClear:hover {
    background: @hover;
    color: @text;
}

QFrame#launchFilterBar {
    background: @overlayBg;
    border: 1px solid @borderSoft;
    border-radius: 6px;
}

QComboBox#launchFilterCombo {
    background: @raisedBg;
    color: @text;
    border: 1px solid @borderSoft;
    border-radius: 5px;
    padding: 3px 16px 3px 7px;
    min-height: 20px;
}

QComboBox#launchFilterCombo:hover,
QComboBox#launchFilterCombo:focus {
    background: @hover;
    color: @textStrong;
    border-color: @primary;
}

QComboBox#launchFilterCombo::drop-down {
    subcontrol-origin: padding;
    subcontrol-position: top right;
    width: 16px;
    border-left: 1px solid @borderSoft;
}

QComboBox#launchFilterCombo::down-arrow {
    width: 9px;
    height: 9px;
}

QComboBox#launchFilterCombo QAbstractItemView {
    background: @panelBg;
    color: @text;
    border: 1px solid @border;
    selection-background-color: @hover;
    selection-color: @textStrong;
    outline: 0;
}

QTextBrowser#chatLog,
QTextBrowser#browserFallback,
QFrame#browserBody,
QFrame#browserMain,
QListWidget#membersList,
QListWidget#sideDrawerList,
QListWidget#launchList,
QListWidget#launchServersList,
QTableWidget {
    background: @windowBg;
    color: @text;
    border-color: @border;
}

QListWidget#launchList {
    show-decoration-selected: 0;
}

QTextBrowser#chatLog {
    font-size: @chatFontSizepx;
}

QWidget#launchServerCard {
    background: @raisedBg;
    border: 1px solid @borderSoft;
    border-radius: 8px;
}

QWidget#launchServerCard[expanded="true"] {
    background: @hover;
    border-color: @primary;
}

QLabel#launchServerName {
    color: @textStrong;
    font-weight: 800;
    font-size: @baseFontSizepx;
}

QLabel#launchServerMeta {
    color: @textMuted;
    font-weight: 700;
    font-size: @sectionFontSizepx;
}

QLabel#launchServerDescription {
    color: @text;
    font-size: @smallFontSizepx;
}

QFrame#membersPanel,
QFrame#sideDrawer,
QFrame#sideDrawerHeader,
QFrame#membersActions,
QFrame#launchLibraryPanel,
QFrame#launchDetailPanel {
    background: @panelBg;
    border-color: @border;
}

QFrame#launchInfoOverlay,
QFrame#launchActionsOverlay,
QFrame#launchServersOverlay {
    background: @overlayBg;
    border: 1px solid @overlayBorder;
}

QLabel#sectionLabel,
QLabel#launchPath,
StatusLine QLabel,
QTextBrowser#chatLog .time {
    color: @textDim;
    font-size: @sectionFontSizepx;
}

QLabel#launchSubtitle,
QLabel#launchServersStatus,
QTextBrowser#chatLog .sys {
    color: @textMuted;
    font-size: @smallFontSizepx;
}

QLabel#launchTitle {
    color: @textStrong;
    font-size: @launchTitleFontSizepx;
}

QLabel#heroTitle,
QLabel#browserHero {
    color: @textStrong;
    font-size: @heroTitleFontSizepx;
}

QLabel#loginBrand {
    color: @textStrong;
    font-size: @brandTitleFontSizepx;
}

QLabel#statusName {
    color: @primary;
}

QLabel#statusOnline[online="true"],
QLabel#statusDot,
QLabel#pillReady {
    color: @success;
}

QLabel#statusOnline,
QLabel#pillUpdating {
    color: @warning;
}

QTextBrowser#chatLog .flag,
QLabel#statusFlag[kind="mod"],
QLabel#pillMissing,
QLabel#pillFull {
    color: @danger;
}

QLabel#statusFlag[kind="dev"] {
    color: @purple;
}

QLabel#statusFlag[kind="free"] {
    color: @success;
}

QLabel#statusFlag {
    background: transparent;
    border: 0;
    padding: 0;
}

QPushButton#sendButton,
QPushButton#chatTextButton {
    background: @toolbarBg;
    border: 1px solid @border;
}

QPushButton#sendButton:hover,
QPushButton#chatTextButton:hover {
    background: @hover;
    border-color: @primary;
}

QToolButton#sidePanelButton {
    background: transparent;
    color: @text;
    border: 1px solid transparent;
    border-radius: 5px;
}

QToolButton#sidePanelButton:hover,
QToolButton#sidePanelButton:pressed,
QToolButton#sidePanelButton:checked {
    background: @hover;
    color: @textStrong;
    border-color: @primary;
}

QLabel#pillReady {
    background: @successBadge;
}

QLabel#pillMissing,
QLabel#pillFull {
    background: @dangerBadge;
}

QLabel#pillUpdating {
    background: @warningBadge;
}

QLabel#pillNeutral {
    background: @neutralBadge;
    color: @textMuted;
}

QDialog#loginDialog {
    background: @pageBg;
    color: @text;
}

QFrame#loginPanel {
    background: @panelBg;
    border: 1px solid @border;
    border-radius: 8px;
}

QDialog#loginDialog QLabel,
QFrame#loginPanel QLabel,
QFrame#loginPanel QCheckBox {
    background: transparent;
}

QLabel#loginSubtitle {
    color: @textDim;
}

QLabel#loginMessage {
    color: @textMuted;
    background: transparent;
    border: 1px solid transparent;
    border-radius: 8px;
    padding: 6px 8px;
}

QLabel#loginMessage[error="true"] {
    color: @danger;
    background: @dangerBadge;
    border-color: @danger;
}

QPushButton#primaryButton {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @primary, stop:1 @primary2);
    color: @textStrong;
    border: 0;
}

QPushButton#playButton {
    background: @raisedBg;
    color: @primary;
    border: 1px solid @primary;
    font-weight: 800;
}

QPushButton#playButton:hover {
    background: @hover;
    border-color: @primary2;
    color: @textStrong;
}

QPushButton#chatTextButton {
    color: @warning;
}

QHeaderView::section {
    background: @raisedBg;
    color: @textMuted;
    border-color: @border;
    font-size: @baseFontSizepx;
}

QTableWidget {
    gridline-color: @borderSoft;
    selection-background-color: @selection;
    selection-color: @textStrong;
}

QTableWidget::item {
    border-bottom: 1px solid @borderSoft;
}

QScrollBar:vertical,
QScrollBar:horizontal {
    background: @windowBg;
}

QScrollBar::handle {
    background: @border;
}

QScrollBar::handle:hover {
    background: @hover;
}
)";

    const QHash<QString, QString> replacements = {
        {"@fontFamily", gamecq::theme_settings::qssString(fontFamilyForTheme(theme))},
        {"@baseFontSize", numericString("baseFontSize", theme)},
        {"@smallFontSize", numericString("smallFontSize", theme)},
        {"@sectionFontSize", numericString("sectionFontSize", theme)},
        {"@chatFontSize", numericString("chatFontSize", theme)},
        {"@launchTitleFontSize", numericString("launchTitleFontSize", theme)},
        {"@heroTitleFontSize", numericString("heroTitleFontSize", theme)},
        {"@brandTitleFontSize", numericString("brandTitleFontSize", theme)},
        {"@windowBg", windowBg},
        {"@pageBg", pageBg},
        {"@panelBg", panelBg},
        {"@raisedBg", raisedBg},
        {"@toolbarBg", toolbarBg},
        {"@inputBg", inputBg},
        {"@textStrong", textStrong},
        {"@textMuted", textMuted},
        {"@textDim", textDim},
        {"@text", text},
        {"@borderSoft", borderSoft},
        {"@border", border},
        {"@hover", hover},
        {"@selection", selection},
        {"@primary2", primary2},
        {"@primary", primary},
        {"@success", success},
        {"@warning", warning},
        {"@danger", danger},
        {"@blue", blue},
        {"@purple", purple},
        {"@overlayBg", rgba(windowBg, 184)},
        {"@overlayBorder", rgba(border, 176)},
        {"@successBadge", rgba(success, 38)},
        {"@warningBadge", rgba(warning, 38)},
        {"@dangerBadge", rgba(danger, 38)},
        {"@purpleBadge", rgba(purple, 42)},
        {"@neutralBadge", rgba(textMuted, 28)},
    };

    QStringList keys = replacements.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right) {
        return left.size() > right.size();
    });
    for (const QString &key : keys) {
        qss.replace(key, replacements.value(key));
    }

    qss += gamecq::theme_styles::presetPolishOverrides(theme, replacements);
    return qss;
}


} // namespace

namespace ThemeManager {

QString buildStyleSheet(const QStringList &arguments)
{
    const QString baseTheme = activeBaseTheme(arguments);
    const QString base = readResourceText(resourcePathForTheme(baseTheme));
    return base + themeOverrides(baseTheme);
}

QString chatDocumentStyleSheet()
{
    const QString theme = configuredBaseTheme();
    QString qss = R"(
p { margin: 0 0 6px 0; color: @text; font-family: @fontFamily, Tahoma, sans-serif; font-size: @chatFontSizepx; }
.time { color: @textDim; }
.sys { color: @textMuted; }
.history { opacity: 0.82; }
.history-divider { margin: 8px 0 10px 0; padding-top: 8px; border-top: 1px solid @border; color: @textMuted; }
.history-divider span { color: @textMuted; }
.pm { color: @blue; font-weight: 700; }
.author { font-weight: 700; }
.author-primary { color: @primary; }
.author-success { color: @success; }
a { color: @blue; text-decoration: underline; }
.discord-media { width: fit-content; max-width: 360px; margin: 3px 0 9px 0; padding: 0; background: transparent; border: 0; }
.discord-media img { display: block; width: auto; max-width: 360px; max-height: 240px; margin: 0; object-fit: contain; background: transparent; }
.discord-media iframe { display: block; width: 360px; max-width: 100%; height: 240px; margin: 0; border: 0; background: transparent; }
)";
    const QHash<QString, QString> replacements = {
        {"@fontFamily", gamecq::theme_settings::qssString(fontFamilyForTheme(theme))},
        {"@chatFontSize", numericString("chatFontSize", theme)},
        {"@textDim", colorValue("textDim", theme)},
        {"@textMuted", colorValue("textMuted", theme)},
        {"@text", colorValue("text", theme)},
        {"@primary", colorValue("primary", theme)},
        {"@success", colorValue("success", theme)},
        {"@blue", colorValue("blue", theme)},
        {"@raisedBg", colorValue("raisedBg", theme)},
        {"@border", colorValue("border", theme)},
    };
    QStringList keys = replacements.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &left, const QString &right) {
        return left.size() > right.size();
    });
    for (const QString &key : keys) {
        qss.replace(key, replacements.value(key));
    }
    return qss;
}


} // namespace ThemeManager
