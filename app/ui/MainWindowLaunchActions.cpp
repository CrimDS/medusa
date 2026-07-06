#include "ui/MainWindow.h"

#include "launcher/LaunchCatalog.h"
#include "launcher/LaunchEntryDialog.h"
#include "launcher/HttpManifestUpdater.h"
#include "launcher/LaunchMetadata.h"
#include "launcher/LaunchRunner.h"
#include "ui/LaunchInfoList.h"
#include "ui/LegacyIcons.h"
#include "ui/MainWindowLaunchSupport.h"

#include <QAction>
#include <QDesktopServices>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QUrl>

using namespace gamecq::launch_ui;
void MainWindow::rebuildLaunchActionsMenu()
{
    if (!m_launchActionsMenu) {
        return;
    }

    m_launchCancelAction = nullptr;
    m_launchActionsMenu->clear();

    const QString launchId = selectedLaunchId();
    const LaunchEntry entry = findLaunchEntry(launchId);
    const bool legacyRunning = m_legacyMirrorProcess && m_legacyMirrorProcess->state() != QProcess::NotRunning;
    const bool updateRunning = (m_d12Updater && m_d12Updater->isRunning()) || legacyRunning;

    if (!hasLaunchEntry(entry)) {
        auto *empty = m_launchActionsMenu->addAction("Select an entry first");
        empty->setEnabled(false);
        m_launchActionsMenu->addSeparator();
        m_launchActionsMenu->addAction("Add Game or Software...", this, &MainWindow::showAddLaunchEntryDialog);
        return;
    }

    auto *title = m_launchActionsMenu->addAction(QString("%1  %2").arg(entry.name, entry.variant));
    title->setEnabled(false);
    m_launchActionsMenu->addSeparator();

    if (entry.stubOnly) {
        auto *stub = m_launchActionsMenu->addAction("Stubbed for now");
        stub->setEnabled(false);
    }

    if ((entry.httpManifestUpdater || usesLegacyMirrorUpdater(entry)) && canBypassLaunchUpdates()) {
        auto *skipUpdate = m_launchActionsMenu->addAction("Launch Without Updating", this, &MainWindow::runSelectedLaunchEntryWithoutUpdateCheck);
        skipUpdate->setEnabled(!entry.stubOnly && !updateRunning);
    }
    if (entry.id == "darkspace-d12-live") {
        auto *installFolder = m_launchActionsMenu->addAction("Set Install Folder...", this, &MainWindow::chooseD12InstallFolder);
        installFolder->setEnabled(!updateRunning);
    }

    if (!entry.needsServer && !launchSteamAppId(entry).isEmpty()) {
        m_launchActionsMenu->addAction("Probe Steam Servers", this, &MainWindow::probeSelectedLaunchServers);
    }

    if (!entry.stubOnly) {
        m_launchActionsMenu->addAction("Open Install Folder", this, [this, entry]() {
            const QString folder = launchInstallRoot(entry);
            if (!folder.isEmpty()) {
                QDir().mkpath(folder);
                QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
            }
        });
    }
    if (entry.customEntry) {
        m_launchActionsMenu->addAction("Edit Details and Artwork...", this, &MainWindow::showEditLaunchEntryDialog);
        m_launchActionsMenu->addAction("Open Artwork Folder", this, &MainWindow::openSelectedLaunchArtworkFolder);
    } else {
        auto *artwork = m_launchActionsMenu->addMenu("Artwork");
        artwork->addAction("Open Artwork Folder", this, &MainWindow::openSelectedLaunchArtworkFolder);
        artwork->addAction("Choose Banner Image...", this, [this]() {
            chooseLaunchArtwork("banner");
        });
        artwork->addAction("Choose Capsule Image...", this, [this]() {
            chooseLaunchArtwork("capsule");
        });
    }
    if (updateRunning) {
        m_launchActionsMenu->addSeparator();
        m_launchCancelAction = m_launchActionsMenu->addAction("Cancel Update", this, &MainWindow::cancelLaunchUpdate);
        m_launchCancelAction->setEnabled(updateRunning);
    }

    if (entry.customEntry || !entry.stubOnly) {
        m_launchActionsMenu->addSeparator();
        auto *remove = m_launchActionsMenu->addAction(entry.customEntry ? "Remove from Launch" : "Delete Installed Files", this, &MainWindow::deleteSelectedLaunchEntry);
        remove->setEnabled(!updateRunning);
    }
}


void MainWindow::showLaunchContextMenu(const QPoint &position)
{
    if (m_launchList) {
        QListWidgetItem *item = m_launchList->itemAt(position);
        if (!item) {
            return;
        }

        m_launchList->setCurrentItem(item);
        if (m_launchActionsMenu) {
            rebuildLaunchActionsMenu();
            m_launchActionsMenu->exec(m_launchList->viewport()->mapToGlobal(position));
        }
        return;
    }

}


