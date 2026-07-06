#include "ui/MainWindow.h"

#include "launcher/LaunchCatalog.h"
#include "launcher/HttpManifestUpdater.h"
#include "launcher/LaunchMetadata.h"
#include "ui/LaunchInfoList.h"
#include "ui/LaunchPresentation.h"
#include "ui/MainWindowLaunchSupport.h"

#include <QFontMetrics>
#include <QFutureWatcher>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QStatusBar>
#include <QtConcurrent>

using namespace gamecq::launch_ui;

void MainWindow::populateLaunchServerList(const QString &launchId)
{
    if (!m_launchServersList) {
        return;
    }

    m_launchServersList->clear();
    const LaunchEntry entry = findLaunchEntry(launchId);
    populateDarkSpaceUpdatesPanel(launchId);
    if (!hasLaunchEntry(entry)) {
        if (m_launchServersPanel) {
            m_launchServersPanel->setVisible(false);
        }
        if (m_launchServersStatus) {
            m_launchServersStatus->clear();
        }
        return;
    }

    if (!entry.needsServer) {
        const QList<ServerInfo> discoveredServers = m_launchDiscoveredServers.value(launchId);
        // Non-DarkSpace entries reuse this panel for optional Steam server
        // probes and launcher information instead of showing an empty server UI.
        if (m_launchServersPanel) {
            m_launchServersPanel->setVisible(true);
        }
        if (m_launchServersTitle) {
            m_launchServersTitle->setText(discoveredServers.isEmpty() ? "Updates + News" : "Servers + News");
        }
        if (m_launchServersStatus) {
            if (discoveredServers.isEmpty()) {
                m_launchServersStatus->clear();
            } else {
                m_launchServersStatus->setText(discoveredServers.size() == 1 ? "1 Steam server" : QString("%1 Steam servers").arg(discoveredServers.size()));
            }
        }
        if (m_launchServersRefreshButton) {
            m_launchServersRefreshButton->setText("Probe");
            m_launchServersRefreshButton->setVisible(false);
            m_launchServersRefreshButton->setEnabled(false);
        }
        if (!discoveredServers.isEmpty()) {
            for (const ServerInfo &server : discoveredServers) {
                addLaunchServerItem(m_launchServersList, server, false, true);
            }
        }
        populateLaunchInfoItems(m_launchServersList, entry);
        return;
    }

    if (m_launchServersPanel) {
        m_launchServersPanel->setVisible(true);
    }
    if (m_launchServersTitle) {
        m_launchServersTitle->setText("Servers");
    }
    if (m_launchServersRefreshButton) {
        m_launchServersRefreshButton->setText("Refresh");
        m_launchServersRefreshButton->setVisible(true);
        const bool legacyRunning = m_legacyMirrorProcess && m_legacyMirrorProcess->state() != QProcess::NotRunning;
        const bool updateRunning = (m_d12Updater && m_d12Updater->isRunning()) || legacyRunning;
        m_launchServersRefreshButton->setEnabled(!updateRunning);
    }

    int matchingServers = 0;
    for (const ServerInfo &server : m_servers) {
        if (!isStaffProfile() && !isPublicServerRow(server)) {
            // The worker also filters non-staff requests. Keep this UI-side
            // guard so cached staff-only rows cannot leak after a profile change.
            continue;
        }
        if (server.gameId != entry.lobbyId || server.type != entry.serverType || server.address.isEmpty() || server.port == 0) {
            continue;
        }

        addLaunchServerItem(m_launchServersList, server, true, isStaffProfile());
        ++matchingServers;
    }

    if (m_launchServersStatus) {
        m_launchServersStatus->setText(matchingServers == 1 ? "1 server" : QString("%1 servers").arg(matchingServers));
    }

    if (matchingServers == 0) {
        if (m_launchServersTitle) {
            m_launchServersTitle->setText(isPrimaryDarkSpaceLaunchEntry(entry) ? "Servers" : "Updates + News");
        }
        if (m_launchServersStatus) {
            m_launchServersStatus->setText(m_servers.isEmpty() ? "No servers loaded" : "No matching servers");
        }
        const QString message = m_servers.isEmpty() ? "No servers loaded. Press Refresh." : "No matching servers loaded.";
        addLaunchInfoItem(
            m_launchServersList,
            message,
            isPrimaryDarkSpaceLaunchEntry(entry) ? "DarkSpace development updates are shown on the right." : "Showing available updates and news instead.");
        if (!isPrimaryDarkSpaceLaunchEntry(entry)) {
            populateLaunchInfoItems(m_launchServersList, entry);
        }
    }
}


void MainWindow::updateLaunchServerCardExpansion()
{
    if (!m_launchServersList) {
        return;
    }

    const int textWidth = qMax(180, m_launchServersList->viewport()->width() - 78);
    for (int row = 0; row < m_launchServersList->count(); ++row) {
        QListWidgetItem *item = m_launchServersList->item(row);
        if (!item) {
            continue;
        }
        if (item->data(kLaunchInfoTitleRole).isValid()) {
            continue;
        }

        QWidget *card = m_launchServersList->itemWidget(item);
        if (!card) {
            continue;
        }

        const bool expanded = item == m_launchServersList->currentItem();
        const QString summary = item->data(kServerSummaryRole).toString();
        const QString details = item->data(kServerDetailsRole).toString();
        const QString text = expanded && !details.isEmpty() ? details : summary;

        // Cards stay compact until selected. This keeps the launcher usable at
        // smaller window sizes while still exposing full server descriptions.
        auto *body = card->findChild<QLabel *>("launchServerDescription");
        if (body) {
            body->setText(text);
            body->setVisible(!text.isEmpty());
        }

        int height = text.isEmpty() ? 78 : 112;
        if (expanded && !text.isEmpty()) {
            const QFontMetrics metrics(body ? body->font() : font());
            const QRect textBounds = metrics.boundingRect(
                QRect(0, 0, textWidth, 4000),
                Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                text);
            height = qMax(112, 92 + textBounds.height());
        }

        item->setSizeHint(QSize(0, height));
        card->setProperty("expanded", expanded);
        polishObjectName(card);
    }

    m_launchServersList->doItemsLayout();
    m_launchServersList->viewport()->update();
    updateLaunchInfoCardExpansion(m_launchServersList);

    const LaunchEntry entry = findLaunchEntry(selectedLaunchId());
    if (m_launchPlayButton && hasLaunchEntry(entry) && entry.needsServer) {
        QListWidgetItem *selected = m_launchServersList->currentItem();
        const bool hasServer = selected
            && !selected->data(kServerAddressRole).toString().isEmpty()
            && selected->data(kServerPortRole).toInt() != 0;
        m_launchPlayButton->setText(hasServer ? "Play" : "Select Server");
    }
}


void MainWindow::probeSelectedLaunchServers()
{
    const QString launchId = selectedLaunchId();
    const LaunchEntry entry = findLaunchEntry(launchId);
    if (!hasLaunchEntry(entry)) {
        statusBar()->showMessage("Select a Steam game or app first.", 3000);
        return;
    }

    const QString appId = launchSteamAppId(entry);
    if (appId.isEmpty()) {
        statusBar()->showMessage(QString("%1 does not have a Steam AppID to probe.").arg(entry.name), 4000);
        return;
    }
    if (m_launchServerProbeIds.contains(launchId)) {
        statusBar()->showMessage(QString("Already probing servers for %1.").arg(entry.name), 3000);
        return;
    }
    m_launchServerProbeIds.insert(launchId);

    if (m_launchServersPanel) {
        m_launchServersPanel->setVisible(true);
    }
    if (m_launchServersList) {
        m_launchServersList->clear();
        auto *item = new QListWidgetItem(QString("Probing Steam servers for AppID %1...").arg(appId), m_launchServersList);
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    if (m_launchServersStatus) {
        m_launchServersStatus->setText("Probing...");
    }
    if (m_launchServersRefreshButton) {
        m_launchServersRefreshButton->setText("Probe");
        m_launchServersRefreshButton->setEnabled(false);
    }

    auto *watcher = new QFutureWatcher<SteamServerProbeResult>(this);
    connect(watcher, &QFutureWatcher<SteamServerProbeResult>::finished, this, [this, watcher, launchId, entry]() {
        const SteamServerProbeResult result = watcher->result();
        watcher->deleteLater();
        m_launchServerProbeIds.remove(launchId);

        if (result.servers.isEmpty()) {
            m_launchDiscoveredServers.remove(launchId);
            populateLaunchServerList(launchId);
            const QString message = result.error.isEmpty()
                ? QString("No public Steam servers were found for %1.").arg(entry.name)
                : QString("Steam server probe failed for %1: %2").arg(entry.name, result.error);
            statusBar()->showMessage(message, 7000);
            return;
        }

        m_launchDiscoveredServers.insert(launchId, result.servers);
        populateLaunchServerList(launchId);
        statusBar()->showMessage(QString("Found %1 Steam server%2 for %3.")
                                     .arg(result.servers.size())
                                     .arg(result.servers.size() == 1 ? QString() : QString("s"), entry.name),
                                 7000);
    });
    watcher->setFuture(QtConcurrent::run([launchId, appId]() {
        // Steam master-server probing can block on DNS/UDP timeouts. Keep it off
        // the UI thread and merge the result back through QFutureWatcher.
        SteamServerProbeResult result;
        result.launchId = launchId;
        result.appId = appId;
        result.servers = querySteamMasterServers(appId, &result.error);
        return result;
    }));
}


