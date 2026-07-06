#include "ui/MainWindow.h"

#include "launcher/HttpManifestUpdater.h"
#include "launcher/LaunchCatalog.h"
#include "launcher/LegacyUpdateRunner.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>

void MainWindow::wireLaunchUpdaters()
{
    connect(m_d12Updater, &HttpManifestUpdater::statusChanged, this, [this](const QString &status) {
        if (!m_launchUpdateId.isEmpty()) {
            m_launchUpdateStatus = status;
            m_launchUpdateDetail = status;
            refreshLaunchStatus();
        }
        statusBar()->showMessage(status, 5000);
    });
    connect(m_d12Updater, &HttpManifestUpdater::progressChanged, this, [this](int completed, int total, const QString &path) {
        if (!m_launchUpdateId.isEmpty()) {
            m_launchUpdateStatus = total > 0 ? QString("Updating %1/%2").arg(completed).arg(total) : "Updating";
            m_launchUpdateDetail = QDir::toNativeSeparators(path);
            refreshLaunchStatus();
        }
        statusBar()->showMessage(QString("D12 update %1/%2: %3").arg(completed).arg(total).arg(path), 5000);
    });
    connect(m_d12Updater, &HttpManifestUpdater::finished, this, [this](bool success, const QString &message) {
        // Copy the pending launch state before clearing UI update state. The
        // continuation is posted back to the event loop so the updater can fully
        // unwind before a game process is started.
        const QString pendingLaunchId = m_pendingLaunchId;
        const ServerInfo pendingServer = m_pendingLaunchServer;
        const bool shouldLaunchAfterUpdate = success && m_pendingLaunchAfterUpdate;
        const bool hasPendingServer = m_pendingLaunchHasServer;

        m_launchUpdateId.clear();
        m_launchUpdateStatus.clear();
        m_launchUpdateDetail.clear();
        if (m_launchCancelAction) {
            m_launchCancelAction->setEnabled(false);
        }
        populateLaunchList();
        if (success) {
            statusBar()->showMessage(message, 6000);
            if (shouldLaunchAfterUpdate) {
                clearPendingLaunch();
                const LaunchEntry entry = findLaunchEntry(pendingLaunchId);
                if (hasLaunchEntry(entry) && QFileInfo::exists(resolveLaunchPath(entry, entry.executable))) {
                    QTimer::singleShot(0, this, [this, pendingLaunchId, pendingServer, hasPendingServer]() {
                        startLaunchEntry(pendingLaunchId, hasPendingServer ? &pendingServer : nullptr, true);
                    });
                } else {
                    statusBar()->showMessage("D12 update finished, but the client executable was not found in the install folder.", 7000);
                }
            } else {
                clearPendingLaunch();
            }
        } else if (message.contains("cancelled", Qt::CaseInsensitive)) {
            clearPendingLaunch();
            statusBar()->showMessage(message, 5000);
        } else {
            clearPendingLaunch();
            QMessageBox::warning(this, "D12 Update Failed", message);
        }
    });
}

void MainWindow::clearPendingLaunch()
{
    m_pendingLaunchId.clear();
    m_pendingLaunchServer = {};
    m_pendingLaunchHasServer = false;
    m_pendingLaunchAfterUpdate = false;
}

void MainWindow::startHttpManifestUpdate(const QString &launchId, bool launchAfterUpdate, const ServerInfo *server)
{
    const LaunchEntry entry = findLaunchEntry(launchId);
    if (!hasLaunchEntry(entry) || !entry.httpManifestUpdater) {
        statusBar()->showMessage("Select a manifest-managed client first.", 3000);
        return;
    }
    if (entry.stubOnly) {
        statusBar()->showMessage(QString("%1 %2 is stubbed for now.").arg(entry.name, entry.variant), 4000);
        return;
    }
    if (m_d12Updater->isRunning()) {
        statusBar()->showMessage("D12 update is already running.", 4000);
        return;
    }
    if (m_legacyMirrorProcess && m_legacyMirrorProcess->state() != QProcess::NotRunning) {
        statusBar()->showMessage("Wait for the legacy DarkSpace update to finish first.", 4000);
        return;
    }

    clearPendingLaunch();
    if (launchAfterUpdate) {
        // Store launch intent separately from the updater object. The same
        // updater is used for manual repair and update-before-play flows.
        m_pendingLaunchId = launchId;
        m_pendingLaunchAfterUpdate = true;
        if (server) {
            m_pendingLaunchServer = *server;
            m_pendingLaunchHasServer = true;
        }
    }

    const QString installRoot = launchInstallRoot(entry);
    m_launchUpdateId = entry.id;
    m_launchUpdateStatus = "Checking D12 manifest...";
    m_launchUpdateDetail = QDir::toNativeSeparators(installRoot);
    if (m_launchCancelAction) {
        m_launchCancelAction->setEnabled(true);
    }
    refreshLaunchStatus();
    m_d12Updater->start(QUrl(entry.manifestUrl), installRoot);
}

void MainWindow::startLegacyMirrorUpdate(const QString &launchId, bool launchAfterUpdate, const ServerInfo *server)
{
    const LaunchEntry entry = findLaunchEntry(launchId);
    if (!hasLaunchEntry(entry) || !usesLegacyMirrorUpdater(entry)) {
        statusBar()->showMessage("Select a legacy DarkSpace client first.", 3000);
        return;
    }
    if (entry.stubOnly) {
        statusBar()->showMessage(QString("%1 %2 is stubbed for now.").arg(entry.name, entry.variant), 4000);
        return;
    }
    if (m_d12Updater && m_d12Updater->isRunning()) {
        statusBar()->showMessage("Wait for the D12 update to finish first.", 4000);
        return;
    }
    if (m_legacyMirrorProcess && m_legacyMirrorProcess->state() != QProcess::NotRunning) {
        statusBar()->showMessage("Legacy DarkSpace update is already running.", 4000);
        return;
    }

    const QString tool = findLegacyD9UpdateToolPath();
    if (tool.isEmpty()) {
        QMessageBox::warning(this,
                             "Legacy Updater Missing",
                             QString("Could not find the legacy x86 updater helper (%1).").arg(legacyD9UpdateToolFileName()));
        return;
    }

    clearPendingLaunch();
    if (launchAfterUpdate) {
        m_pendingLaunchId = launchId;
        m_pendingLaunchAfterUpdate = true;
        if (server) {
            m_pendingLaunchServer = *server;
            m_pendingLaunchHasServer = true;
        }
    }

    const QString installRoot = launchInstallRoot(entry);
    if (!QDir().mkpath(installRoot)) {
        QMessageBox::warning(this, "Legacy Update Failed", QString("Could not create %1.").arg(QDir::toNativeSeparators(installRoot)));
        clearPendingLaunch();
        return;
    }

    m_launchUpdateId = entry.id;
    m_launchUpdateStatus = "Checking legacy mirror...";
    m_launchUpdateDetail = legacyD9MirrorStatusDetail(tool);
    m_legacyMirrorCancelRequested = false;
    m_legacyMirrorProgressPercent = -1;
    refreshLaunchStatus();
    statusBar()->showMessage("Checking original D9 mirror...", 5000);

    auto *process = new QProcess(this);
    m_legacyMirrorProcess = process;
    configureLegacyUpdateProcess(process, tool, installRoot, m_sessionId);
    const bool usingClientUpdateHelper = legacyUpdateUsesClientHelper(tool);

    // The bundled helper prints progress lines. Older fallback tools may not,
    // so only connect live output parsing when the helper supports it.
    auto readLegacyOutput = [this, process]() {
        const QString line = latestLegacyUpdateOutputLine(process);
        if (line.isEmpty()) {
            return;
        }
        m_launchUpdateDetail = line;
        const int progressPercent = legacyUpdatePercentForLine(line);
        if (progressPercent >= 0) {
            m_legacyMirrorProgressPercent = qMax(m_legacyMirrorProgressPercent, progressPercent);
            m_launchUpdateStatus = QString("Downloading legacy files... %1%").arg(m_legacyMirrorProgressPercent);
        } else if (m_legacyMirrorProgressPercent >= 0) {
            m_launchUpdateStatus = QString("Downloading legacy files... %1%").arg(m_legacyMirrorProgressPercent);
        } else {
            m_launchUpdateStatus = legacyUpdateStatusForLine(line);
        }
        refreshLaunchStatus();
        statusBar()->showMessage(line, 5000);
    };

    if (usingClientUpdateHelper) {
        connect(process, &QProcess::readyRead, this, readLegacyOutput);
    }
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_legacyMirrorProcess != process) {
            process->deleteLater();
            return;
        }
        const QString processOutput = QString::fromLocal8Bit(process->readAll()).trimmed();
        const QString detail = latestLegacyUpdateDetail(processOutput, m_launchUpdateDetail);

        const QString pendingLaunchId = m_pendingLaunchId;
        const ServerInfo pendingServer = m_pendingLaunchServer;
        const bool shouldLaunchAfterUpdate = m_pendingLaunchAfterUpdate;
        const bool hasPendingServer = m_pendingLaunchHasServer;
        const bool cancelled = m_legacyMirrorCancelRequested;
        // Exit code 0 means no changes were needed; exit code 1 means files
        // were updated. Both are successful legacy updater outcomes.
        const bool success = exitStatus == QProcess::NormalExit && (exitCode == 0 || exitCode == 1);

        m_legacyMirrorProcess = nullptr;
        m_legacyMirrorCancelRequested = false;
        m_legacyMirrorProgressPercent = -1;
        m_launchUpdateId.clear();
        m_launchUpdateStatus.clear();
        m_launchUpdateDetail.clear();
        if (m_launchCancelAction) {
            m_launchCancelAction->setEnabled(false);
        }
        process->deleteLater();
        populateLaunchList();

        if (cancelled) {
            clearPendingLaunch();
            statusBar()->showMessage("Legacy DarkSpace update cancelled.", 5000);
            return;
        }

        if (!success) {
            clearPendingLaunch();
            QMessageBox::warning(this,
                                 "Legacy Update Failed",
                                 QString("Legacy updater helper exited with code %1.%2")
                                     .arg(exitCode)
                                     .arg(detail.isEmpty() ? QString() : QString("\n\n%1").arg(detail.left(500))));
            return;
        }

        statusBar()->showMessage(exitCode == 0 ? "Original D9 client is already up to date." : "Original D9 client update complete.", 6000);
        if (shouldLaunchAfterUpdate) {
            clearPendingLaunch();
            const LaunchEntry pending = findLaunchEntry(pendingLaunchId);
            if (hasLaunchEntry(pending) && QFileInfo::exists(resolveLaunchPath(pending, pending.executable))) {
                QTimer::singleShot(0, this, [this, pendingLaunchId, pendingServer, hasPendingServer]() {
                    startLaunchEntry(pendingLaunchId, hasPendingServer ? &pendingServer : nullptr, true);
                });
            } else {
                statusBar()->showMessage("D9 update finished, but the client executable was not found in the legacy cache folder.", 7000);
            }
        } else {
            clearPendingLaunch();
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (m_legacyMirrorProcess != process || error != QProcess::FailedToStart) {
            return;
        }

        m_legacyMirrorProcess = nullptr;
        m_legacyMirrorCancelRequested = false;
        m_legacyMirrorProgressPercent = -1;
        m_launchUpdateId.clear();
        m_launchUpdateStatus.clear();
        m_launchUpdateDetail.clear();
        clearPendingLaunch();
        if (m_launchCancelAction) {
            m_launchCancelAction->setEnabled(false);
        }
        process->deleteLater();
        populateLaunchList();
        QMessageBox::warning(this, "Legacy Update Failed", "Could not start the legacy x86 updater helper.");
    });
    process->start();
}

void MainWindow::cancelLaunchUpdate()
{
    if (m_d12Updater && m_d12Updater->isRunning()) {
        m_d12Updater->cancel();
        return;
    }

    if (m_legacyMirrorProcess && m_legacyMirrorProcess->state() != QProcess::NotRunning) {
        m_legacyMirrorCancelRequested = true;
        m_legacyMirrorProcess->kill();
        statusBar()->showMessage("Cancelling legacy DarkSpace update...", 4000);
    }
}
