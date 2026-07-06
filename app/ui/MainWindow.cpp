#include "ui/MainWindow.h"

#include "launcher/HttpManifestUpdater.h"
#include "net/MetaClientBridge.h"
#include "ui/ChatLogWriter.h"
#include "ui/ChatMediaCache.h"
#include "ui/LegacyIcons.h"
#include "ui/WindowChrome.h"

#include <QDir>
#include <QScrollBar>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_bridge(new MetaClientBridge(this))
    , m_d12Updater(new HttpManifestUpdater(this))
    , m_chatLogWriter(new ChatLogWriter(this))
    , m_chatMediaCache(new ChatMediaCache(this))
{
    setWindowTitle("GameCQ 1 ⅜ - Darkspace");
    setWindowIcon(legacyIcon("GCQL"));
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_NativeWindow, true);
    setMouseTracking(true);
    setMinimumSize(760, 480);
    enableWindowsFramelessResize(this);
    m_chatLogWriter->setErrorCallback([this](const QString &path) {
        statusBar()->showMessage(QString("Could not write chat log: %1").arg(QDir::toNativeSeparators(path)), 7000);
    });
    connect(m_chatMediaCache, &ChatMediaCache::imageReady, this, [this]() {
        if (!m_chatLog) {
            return;
        }
        auto *scroll = m_chatLog->verticalScrollBar();
        const bool stickToBottom = scroll && scroll->value() >= scroll->maximum() - 4;
        refreshChatLogTheme();
        if (scroll && stickToBottom) {
            scroll->setValue(scroll->maximum());
        }
    });
    createMenus();
    createShell();
    createStatusBar();
    createTrayIcon();
    wireBridge();
    applySettings();
    QTimer::singleShot(0, this, [this]() { enableWindowsFramelessResize(this); });
    wireLaunchUpdaters();

    QTimer::singleShot(250, this, &MainWindow::showLoginDialog);
}

