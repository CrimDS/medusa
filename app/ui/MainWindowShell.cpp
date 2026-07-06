#include "ui/MainWindow.h"

#include "net/MetaClientBridge.h"
#include "ui/BrowserSupport.h"
#include "ui/ChatLogWriter.h"
#include "ui/ChatUiSupport.h"
#include "ui/LegacyIcons.h"
#include "ui/StatusLine.h"
#include "ui/ThemeManager.h"
#include "ui/WindowChrome.h"

#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabBar>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

using namespace gamecq::chat_ui;
void MainWindow::createMenus()
{
    auto *chrome = new WindowChromeBar(this);
    auto *chromeLayout = new QGridLayout(chrome);
    chromeLayout->setContentsMargins(8, 0, 6, 0);
    chromeLayout->setHorizontalSpacing(8);
    chromeLayout->setVerticalSpacing(0);
    chromeLayout->setColumnStretch(0, 1);
    chromeLayout->setColumnStretch(1, 0);
    chromeLayout->setColumnStretch(2, 1);

    auto *leftCluster = new QWidget(chrome);
    leftCluster->setObjectName("windowChromeLeft");
    leftCluster->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    auto *leftLayout = new QHBoxLayout(leftCluster);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    auto *appIcon = new QLabel(leftCluster);
    appIcon->setObjectName("windowAppIcon");
    appIcon->setPixmap(legacyIcon("GCQL").pixmap(18, 18));
    appIcon->setFixedSize(22, 24);
    appIcon->setAlignment(Qt::AlignCenter);
    leftLayout->addWidget(appIcon);

    auto *mainMenu = new QMenuBar(leftCluster);
    mainMenu->setObjectName("mainMenuBar");
    mainMenu->setNativeMenuBar(false);
    leftLayout->addWidget(mainMenu);
    chromeLayout->addWidget(leftCluster, 0, 0, Qt::AlignLeft);

    auto *controls = new QWidget(chrome);
    controls->setObjectName("windowControls");
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(2);

    auto *minimize = makeWindowControlButton(WindowControlIcon::Minimize, "Minimize", "windowMinimizeButton");
    connect(minimize, &QToolButton::clicked, this, &QWidget::showMinimized);
    controlsLayout->addWidget(minimize);

    m_maximizeButton = makeWindowControlButton(WindowControlIcon::Maximize, "Maximize", "windowMaximizeButton");
    connect(m_maximizeButton, &QToolButton::clicked, this, &MainWindow::toggleMaximized);
    controlsLayout->addWidget(m_maximizeButton);

    auto *close = makeWindowControlButton(WindowControlIcon::Close, "Close", "windowCloseButton");
    connect(close, &QToolButton::clicked, this, &QWidget::close);
    controlsLayout->addWidget(close);
    chromeLayout->addWidget(controls, 0, 2, Qt::AlignRight);

    auto *gameMenu = mainMenu->addMenu("GameCQ");
    gameMenu->addAction("Login", this, &MainWindow::showLoginDialog);
    gameMenu->addAction("Change Name", this, &MainWindow::showChangeNameDialog);
    m_hideAction = gameMenu->addAction("Hide", this, [this]() {
        m_bridge->sendChatMessage("/hide");
    });
    m_unhideAction = gameMenu->addAction("Unhide", this, [this]() {
        m_bridge->sendChatMessage("/unhide");
    });
    m_hideAction->setVisible(false);
    m_unhideAction->setVisible(false);
    gameMenu->addSeparator();
    gameMenu->addAction("Exit", this, &MainWindow::exitApplication);

    auto *editMenu = mainMenu->addMenu("Edit");
    editMenu->addAction("Find User", this, &MainWindow::showFindUserDialog);
    editMenu->addAction("Ignored Users", this, &MainWindow::showIgnoredUsersDialog);
    editMenu->addAction("Options", this, &MainWindow::showOptionsDialog);

    auto *viewMenu = mainMenu->addMenu("View");
    viewMenu->addAction("Browser", this, [this]() {
        if (m_tabBar) {
            m_tabBar->setCurrentIndex(0);
        }
    });
    viewMenu->addAction("Launch", this, [this]() {
        if (m_tabBar) {
            m_tabBar->setCurrentIndex(1);
        }
    });

    auto *helpMenu = mainMenu->addMenu("Help");
    helpMenu->addAction("Chat Commands", this, &MainWindow::showChatCommandsDialog);
    helpMenu->addAction("Darkspace Manual", this, [this]() {
        if (m_tabBar) {
            m_tabBar->setCurrentIndex(0);
        }
        navigateBrowser(QUrl(darkSpaceManualUrlString()));
    });
    helpMenu->addAction("About GameCQ", this, &MainWindow::showAboutDialog);

    setMenuWidget(chrome);
    updateWindowChromeState();
}

void MainWindow::createShell()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabBar = new QTabBar(root);
    m_tabBar->setObjectName("mainTabs");
    m_tabBar->setExpanding(false);
    m_tabBar->setIconSize(QSize(15, 15));
    m_tabBar->addTab(makeDiamondIcon(ThemeManager::color("primary"), true), "Browser");
    m_tabBar->addTab(makeDiamondIcon(ThemeManager::color("textMuted")), "Launch");
    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::setActiveTab);
    layout->addWidget(m_tabBar);

    m_lobbySplitter = new QSplitter(Qt::Vertical, root);
    m_lobbySplitter->setObjectName("lobbySplitter");
    m_lobbySplitter->setChildrenCollapsible(true);
    m_lobbySplitter->setOpaqueResize(false);
    connect(m_lobbySplitter, &QSplitter::splitterMoved, this, [this]() {
        if (!m_adjustingLobbySplitter) {
            saveWorkspaceSplitSizes();
        }
    });

    m_pages = new QStackedWidget(m_lobbySplitter);
    m_pages->setObjectName("topWorkspace");
    m_pages->setMinimumHeight(0);
    m_pages->addWidget(createBrowserPage());
    m_pages->addWidget(createLaunchPage());
    m_lobbySplitter->addWidget(m_pages);

    QWidget *chatPage = createChatPage();
    chatPage->setObjectName("persistentLobby");
    chatPage->setMinimumHeight(0);
    m_lobbySplitter->addWidget(chatPage);
    m_lobbySplitter->setStretchFactor(0, 1);
    m_lobbySplitter->setStretchFactor(1, 1);
    m_workspaceSplitSizes = {360, 360};
    m_lobbySplitter->setSizes(m_workspaceSplitSizes);
    layout->addWidget(m_lobbySplitter, 1);

    setCentralWidget(root);
    setActiveTab(0);
}

void MainWindow::createStatusBar()
{
    statusBar()->setSizeGripEnabled(false);

    auto *container = new QWidget(statusBar());
    container->setObjectName("statusLineContainer");
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);

    m_statusLine = new StatusLine(container);
    m_statusLine->setObjectName("bottomStatusLine");
    m_statusLine->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(m_statusLine, 0, Qt::AlignCenter);
    layout->addStretch(1);

    statusBar()->addPermanentWidget(container, 1);
}

void MainWindow::applyThemeAppearance()
{
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        ThemeManager::applyTheme(*app);
    }
    refreshChatLogTheme();
    if (auto *sendButton = findChild<QPushButton *>("sendButton")) {
        sendButton->setIcon(makeSendIcon(ThemeManager::color("primary")));
    }
    if (m_tabBar) {
        setActiveTab(m_tabBar->currentIndex());
    }
    if (auto *fallbackBrowser = qobject_cast<QTextBrowser *>(m_browserView)) {
        fallbackBrowser->setHtml(browserStartHtml());
    }
    updateChatColorButton();
}


void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateWindowChromeState();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_exitRequested && m_trayIcon && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
        return;
    }

    m_chatLogWriter->flush();
    QMainWindow::closeEvent(event);
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (handleWindowsFramelessResizeNativeEvent(this, eventType, message, result)) {
        return true;
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::toggleMaximized()
{
    isMaximized() ? showNormal() : showMaximized();
    updateWindowChromeState();
}

void MainWindow::updateWindowChromeState()
{
    if (!m_maximizeButton) {
        return;
    }

    const bool maximized = isMaximized();
    m_maximizeButton->setIcon(makeWindowControlIcon(maximized ? WindowControlIcon::Restore : WindowControlIcon::Maximize));
    m_maximizeButton->setToolTip(maximized ? "Restore" : "Maximize");
}

void MainWindow::setActiveTab(int index)
{
    if (index < 0) {
        return;
    }

    for (int tab = 0; tab < m_tabBar->count(); ++tab) {
        const bool active = tab == index;
        m_tabBar->setTabIcon(tab, makeDiamondIcon(active ? ThemeManager::color("primary") : ThemeManager::color("textMuted"), active));
    }

    if (m_pages) {
        m_pages->setCurrentIndex(index);
    }
    if (m_lobbySplitter) {
        const QList<int> sizes = m_lobbySplitter->sizes();
        if (sizes.size() < 2 || sizes.at(0) < 24) {
            restoreWorkspaceSplitSizes();
        }
    }

    if (index == 1 && m_servers.isEmpty() && m_serverLaunchId.isEmpty()) {
        refreshServers();
    }
}

void MainWindow::saveWorkspaceSplitSizes()
{
    if (!m_lobbySplitter) {
        return;
    }

    const QList<int> sizes = m_lobbySplitter->sizes();
    if (sizes.size() == 2 && sizes.at(0) > 24 && sizes.at(1) > 24) {
        m_workspaceSplitSizes = sizes;
    }
}

void MainWindow::restoreWorkspaceSplitSizes()
{
    if (!m_lobbySplitter) {
        return;
    }

    QList<int> sizes = m_workspaceSplitSizes;
    if (sizes.size() != 2 || sizes.at(0) <= 24 || sizes.at(1) <= 24) {
        const int total = m_lobbySplitter->height();
        const int half = total > 100 ? total / 2 : 380;
        sizes = {half, half};
    }

    m_adjustingLobbySplitter = true;
    m_lobbySplitter->setSizes(sizes);
    m_adjustingLobbySplitter = false;
}

