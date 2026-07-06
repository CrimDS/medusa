#include "ui/MainWindow.h"

#include "launcher/HttpManifestUpdater.h"
#include "launcher/LaunchCatalog.h"
#include "launcher/LaunchRunner.h"
#include "ui/LaunchInfoList.h"
#include "ui/MainWindowLaunchSupport.h"

#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QStatusBar>
#include <QTabBar>

using namespace gamecq::launch_ui;

void MainWindow::runSelectedLaunchEntry()
{
    const QString launchId = selectedLaunchId();
    const LaunchEntry entry = findLaunchEntry(launchId);
    if (!hasLaunchEntry(entry)) {
        statusBar()->showMessage("Select something to launch first.", 3000);
        return;
    }

    if (entry.needsServer) {
        if (m_launchServersList) {
            QListWidgetItem *selected = m_launchServersList->currentItem();
            if (hasLaunchServerEndpoint(selected)) {
                const ServerInfo server = launchServerInfoFromItem(selected);
                startLaunchEntry(launchId, &server);
                return;
            }
        }
        selectServerForLaunch(launchId);
        return;
    }

    startLaunchEntry(launchId);
}

void MainWindow::runSelectedLaunchEntryWithoutUpdateCheck()
{
    if (!canBypassLaunchUpdates()) {
        statusBar()->showMessage("Only admin and developer accounts can launch DarkSpace without checking updates.", 5000);
        return;
    }

    const QString launchId = selectedLaunchId();
    const LaunchEntry entry = findLaunchEntry(launchId);
    if (!hasLaunchEntry(entry)) {
        statusBar()->showMessage("Select something to launch first.", 3000);
        return;
    }
    if (!hasLaunchUpdater(entry)) {
        startLaunchEntry(launchId);
        return;
    }

    if (entry.needsServer) {
        if (m_launchServersList) {
            QListWidgetItem *selected = m_launchServersList->currentItem();
            if (hasLaunchServerEndpoint(selected)) {
                const ServerInfo server = launchServerInfoFromItem(selected);
                startLaunchEntry(launchId, &server, true);
                return;
            }
        }
        selectServerForLaunch(launchId);
        return;
    }

    startLaunchEntry(launchId, nullptr, true);
}

void MainWindow::runRelatedLaunchEntry(const QString &kind)
{
    const LaunchEntry entry = findLaunchEntry(selectedLaunchId());
    const QString relatedId = relatedDarkSpaceLaunchId(entry, kind);
    const LaunchEntry related = findLaunchEntry(relatedId);
    if (!hasLaunchEntry(related) || (related.staffOnly && !isStaffProfile())) {
        statusBar()->showMessage("That DarkSpace option is not available for this entry.", 3000);
        return;
    }

    startLaunchEntry(relatedId);
}


void MainWindow::selectServerForLaunch(const QString &launchId)
{
    const LaunchEntry entry = findLaunchEntry(launchId);
    if (!hasLaunchEntry(entry)) {
        statusBar()->showMessage("Select a DarkSpace client first.", 3000);
        return;
    }
    if (entry.stubOnly) {
        statusBar()->showMessage(QString("%1 %2 is stubbed for now.").arg(entry.name, entry.variant), 4000);
        return;
    }
    if (!entry.needsServer) {
        startLaunchEntry(launchId);
        return;
    }

    m_serverLaunchId = launchId;
    if (m_tabBar) {
        m_tabBar->setCurrentIndex(1);
    }
    setServerFilter(entry.lobbyId, entry.serverType);
    statusBar()->showMessage(QString("Select a server for %1 %2.").arg(entry.name, entry.variant), 5000);
}

bool MainWindow::startLaunchEntry(const QString &launchId, const ServerInfo *server, bool updateAlreadyChecked)
{
    const LaunchEntry entry = findLaunchEntry(launchId);
    if (!hasLaunchEntry(entry)) {
        statusBar()->showMessage("Select something to launch first.", 3000);
        return false;
    }

    if (entry.stubOnly) {
        statusBar()->showMessage(QString("%1 %2 is stubbed for now.").arg(entry.name, entry.variant), 4000);
        return false;
    }

    const bool launchUpdateRunning = (m_d12Updater && m_d12Updater->isRunning())
        || (m_legacyMirrorProcess && m_legacyMirrorProcess->state() != QProcess::NotRunning);
    if (launchEntrySharesCurrentUpdate(entry.id) && launchUpdateRunning) {
        statusBar()->showMessage("Wait for the current update to finish before launching.", 4000);
        return false;
    }

    QString address;
    int port = 0;
    if (entry.needsServer) {
        // DarkSpace clients need a live lobby session because the server launch
        // command includes the selected endpoint and the current session ID.
        if (m_sessionId == 0) {
            statusBar()->showMessage("Log in before launching a server-bound client.", 4000);
            return false;
        }
        if (!server) {
            selectServerForLaunch(entry.id);
            return false;
        }
        if (server->address.isEmpty() || server->port == 0 || server->gameId != entry.lobbyId || server->type != entry.serverType) {
            statusBar()->showMessage(QString("Select a valid server for %1 %2.").arg(entry.name, entry.variant), 5000);
            return false;
        }

        address = server->address;
        port = server->port;
        m_serverLaunchId = entry.id;
    }

    if (hasLaunchUpdater(entry) && !updateAlreadyChecked) {
        // Normal users always update-before-launch. Staff-only bypasses call
        // back into this function with updateAlreadyChecked set.
        if (entry.httpManifestUpdater) {
            startHttpManifestUpdate(entry.id, true, server);
        } else {
            startLegacyMirrorUpdate(entry.id, true, server);
        }
        return false;
    }

    const LaunchRunResult launchResult = startLaunchProcess(entry, address, port, m_sessionId);
    if (launchResult.status == LaunchRunStatus::MissingExecutable) {
        // A missing executable usually means the client has not been installed
        // yet, so route through the matching updater before surfacing failure.
        if (entry.httpManifestUpdater && !updateAlreadyChecked) {
            startHttpManifestUpdate(entry.id, true, server);
            return false;
        }
        if (usesLegacyMirrorUpdater(entry) && !updateAlreadyChecked) {
            startLegacyMirrorUpdate(entry.id, true, server);
            return false;
        }

        statusBar()->showMessage(QString("%1 is not installed yet.").arg(entry.name), 4000);
        return false;
    }

    if (launchResult.status == LaunchRunStatus::FailedToStart) {
        QMessageBox::warning(this, "Launch Failed", QString("Could not launch %1.").arg(QDir::toNativeSeparators(launchResult.executable)));
        return false;
    }

    if (m_launchSort && m_launchSort->currentData().toString() == "lastUsed") {
        populateLaunchList();
    }
    statusBar()->showMessage(QString("Launching %1 %2.").arg(entry.name, entry.variant), 4000);
    return true;
}


