#include "ui/ThemeStylePolish.h"

namespace gamecq::theme_styles {

QString emeraldPolishOverrides(const QHash<QString, QString> &replacements)
{
    return applyThemeTokens(R"(

/* Emerald polish */
QFrame#windowChrome,
QMenuBar,
QTabBar#mainTabs,
QFrame#contextToolbar,
QFrame#composer,
QStatusBar {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @windowBg, stop:0.48 @panelBg, stop:1 @raisedBg);
    border-color: @borderSoft;
}

QFrame#membersPanel,
QFrame#sideDrawer,
QFrame#launchLibraryPanel,
QFrame#launchDetailPanel,
QFrame#launchInfoOverlay,
QFrame#launchActionsOverlay,
QFrame#launchServersOverlay {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @panelBg, stop:1 @windowBg);
    border-color: @border;
}

QTextBrowser#chatLog,
QListWidget#membersList,
QListWidget#sideDrawerList,
QListWidget#launchList,
QListWidget#launchServersList,
QLineEdit,
QTextEdit,
QPlainTextEdit {
    border-color: @borderSoft;
}

QTabBar#mainTabs::tab:selected,
QToolButton#sidePanelButton:checked,
QPushButton#sendButton,
QPushButton#chatTextButton {
    border-color: @primary;
}

QPushButton#playButton,
QPushButton#primaryButton {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @primary, stop:1 @primary2);
    color: @windowBg;
    border-color: @primary2;
}

QListWidget::item:selected {
    border-left: 2px solid @primary;
}
)",
                            replacements);
}

} // namespace gamecq::theme_styles
