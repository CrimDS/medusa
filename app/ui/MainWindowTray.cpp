#include "ui/MainWindow.h"

#include "launcher/LaunchCatalog.h"
#include "ui/LegacyIcons.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QMenu>
#include <QSystemTrayIcon>

namespace {

QString launchActionLabel(const LaunchEntry &entry)
{
    QString label = launchLibraryDisplayName(entry);
    if (label.isEmpty()) {
        label = entry.name;
    }
    return label;
}

} // namespace

void MainWindow::createTrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }

    auto *app = qobject_cast<QApplication *>(QApplication::instance());
    if (app) {
        // Closing the main window hides to tray; the explicit Exit action is the
        // path that should end the process.
        app->setQuitOnLastWindowClosed(false);
    }

    m_trayMenu = new QMenu(this);
    connect(m_trayMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildTrayMenu);

    m_trayIcon = new QSystemTrayIcon(legacyIcon("GCQL"), this);
    m_trayIcon->setToolTip("GameCQ 1 ⅜");
    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            showMainWindowFromTray();
        }
    });
    rebuildTrayMenu();
    m_trayIcon->show();
}

void MainWindow::rebuildTrayMenu()
{
    if (!m_trayMenu) {
        return;
    }

    m_trayMenu->clear();
    m_trayMenu->addAction("Show GCQL", this, &MainWindow::showMainWindowFromTray);

    m_trayMenu->addSeparator();
    auto *darkSpaceMenu = m_trayMenu->addMenu("DarkSpace");
    for (const QString &launchId : {QString("darkspace-d12-live"), QString("darkspace-d9-live")}) {
        const LaunchEntry entry = findLaunchEntry(launchId);
        if (!hasLaunchEntry(entry) || (entry.staffOnly && !isStaffProfile())) {
            continue;
        }
        darkSpaceMenu->addAction(launchActionLabel(entry), this, [this, launchId]() {
            // DarkSpace launch still routes through the normal update/server
            // checks. The tray is only a shortcut into the same launch flow.
            showMainWindowFromTray();
            startLaunchEntry(launchId);
        });
    }

    auto *programsMenu = m_trayMenu->addMenu("Programs");
    int programCount = 0;
    for (const LaunchEntry &entry : launchCatalog()) {
        if (!hasLaunchEntry(entry) || entry.needsServer || entry.staffOnly || isFoldedDarkSpaceUtility(entry)) {
            continue;
        }
        programsMenu->addAction(launchActionLabel(entry), this, [this, id = entry.id]() {
            startLaunchEntry(id);
        });
        ++programCount;
    }
    if (programCount == 0) {
        QAction *empty = programsMenu->addAction("No programs configured");
        empty->setEnabled(false);
    }

    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Exit", this, &MainWindow::exitApplication);
}

void MainWindow::showMainWindowFromTray()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::exitApplication()
{
    m_exitRequested = true;
    if (m_trayIcon) {
        m_trayIcon->hide();
    }
    close();
    QCoreApplication::quit();
}
