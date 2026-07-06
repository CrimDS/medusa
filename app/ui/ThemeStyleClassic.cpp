#include "ui/ThemeStylePolish.h"

namespace gamecq::theme_styles {

QString classicPolishOverrides(const QHash<QString, QString> &replacements)
{
    return applyThemeTokens(R"(

/* Classic GCQL polish */
QFrame#windowChrome,
QTabBar#mainTabs,
QFrame#contextToolbar,
QFrame#composer,
QStatusBar {
    background: @toolbarBg;
}

QTextBrowser#chatLog,
QListWidget#membersList,
QListWidget#sideDrawerList,
QListWidget#launchList,
QListWidget#launchServersList {
    border: 1px solid @border;
}

QFrame#membersPanel,
QFrame#sideDrawer,
QFrame#launchLibraryPanel,
QFrame#launchDetailPanel,
QFrame#launchFilterBar,
QFrame#launchInfoOverlay,
QFrame#launchActionsOverlay,
QFrame#launchServersOverlay {
    border: 1px solid @border;
}

QTabBar#mainTabs::tab:selected {
    background: @raisedBg;
    border: 1px solid @border;
}

QPushButton#playButton,
QPushButton#sendButton,
QPushButton#chatTextButton {
    background: @toolbarBg;
    border: 1px solid @border;
}

QLineEdit,
QTextEdit,
QPlainTextEdit,
QComboBox,
QSpinBox {
    color: #000000;
    selection-color: #ffffff;
}

QFrame#launchFilterBar {
    background: @toolbarBg;
    border: 1px solid @border;
}

QComboBox#launchFilterCombo {
    background: @toolbarBg;
    color: @text;
    border: 1px solid @border;
}

QComboBox#launchFilterCombo:hover,
QComboBox#launchFilterCombo:focus {
    background: @raisedBg;
    border-color: @border;
}

QComboBox#launchFilterCombo QAbstractItemView {
    background: @windowBg;
    color: @text;
    border: 1px solid @border;
}
)",
                            replacements);
}

} // namespace gamecq::theme_styles
