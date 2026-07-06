#include "ui/ThemeStylePolish.h"

namespace gamecq::theme_styles {

QString vaporPolishOverrides(const QHash<QString, QString> &replacements)
{
    return applyThemeTokens(R"(

/* Vapor polish */
QFrame#windowChrome,
QTabBar#mainTabs,
QFrame#contextToolbar,
QFrame#composer,
QStatusBar {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @windowBg, stop:0.52 @toolbarBg, stop:1 @panelBg);
}

QTabBar#mainTabs::tab:selected {
    border-color: @primary;
}

QFrame#membersPanel,
QFrame#sideDrawer,
QFrame#launchLibraryPanel,
QFrame#launchDetailPanel,
QFrame#launchInfoOverlay,
QFrame#launchActionsOverlay,
QFrame#launchServersOverlay {
    border-color: @border;
}

QTextBrowser#chatLog,
QListWidget#membersList,
QListWidget#sideDrawerList,
QListWidget#launchList,
QListWidget#launchServersList,
QLineEdit {
    border-color: @borderSoft;
}

QPushButton#playButton,
QPushButton#sendButton,
QPushButton#chatTextButton,
QToolButton#sidePanelButton {
    border-color: @border;
}
)",
                            replacements);
}

} // namespace gamecq::theme_styles
