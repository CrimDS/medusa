#include "ui/ThemeStylePolish.h"

namespace gamecq::theme_styles {

QString consolePolishOverrides(const QHash<QString, QString> &replacements)
{
    return applyThemeTokens(R"(

/* Console polish */
QFrame#windowChrome,
QMenuBar,
QTabBar#mainTabs,
QFrame#contextToolbar,
QFrame#composer,
QStatusBar {
    background: @windowBg;
    border-color: @border;
}

QFrame#membersPanel,
QFrame#sideDrawer,
QFrame#launchLibraryPanel,
QFrame#launchDetailPanel,
QFrame#launchInfoOverlay,
QFrame#launchActionsOverlay,
QFrame#launchServersOverlay,
QTextBrowser#chatLog,
QListWidget#membersList,
QListWidget#sideDrawerList,
QListWidget#launchList,
QListWidget#launchServersList,
QLineEdit,
QTextEdit,
QPlainTextEdit {
    background: @windowBg;
    border-color: @border;
}

QTabBar#mainTabs::tab:selected,
QToolButton#sidePanelButton:checked,
QPushButton#playButton,
QPushButton#sendButton,
QPushButton#chatTextButton {
    border-color: @primary;
}

QPushButton#primaryButton,
QPushButton#playButton {
    color: @windowBg;
}

QHeaderView::section,
QTableWidget::item {
    border-color: @border;
}
)",
                            replacements);
}

} // namespace gamecq::theme_styles
