#include "ui/MainWindow.h"

#include "launcher/LaunchCatalog.h"
#include "launcher/LaunchMetadata.h"
#include "ui/LaunchInfoList.h"
#include "ui/MainWindowLaunchSupport.h"
#include "ui/LaunchPresentation.h"

#include <QFutureWatcher>
#include <QJsonObject>
#include <QListWidget>
#include <QUrl>
#include <QtConcurrent>

using namespace gamecq::launch_ui;

void MainWindow::populateDarkSpaceUpdatesPanel(const QString &launchId)
{
    if (!m_launchUpdatesPanel || !m_launchUpdatesList) {
        return;
    }

    const LaunchEntry entry = findLaunchEntry(launchId);
    const bool showUpdates = isPrimaryDarkSpaceLaunchEntry(entry);
    m_launchUpdatesPanel->setVisible(showUpdates);
    m_launchUpdatesList->clear();
    if (!showUpdates) {
        if (m_launchUpdatesStatus) {
            m_launchUpdatesStatus->clear();
        }
        return;
    }

    if (m_darkSpaceUpdatesLoading) {
        if (m_launchUpdatesStatus) {
            m_launchUpdatesStatus->setText("Loading...");
        }
        addLaunchInfoItem(m_launchUpdatesList, "Loading development log...", "Fetching the latest DarkSpace update notes.");
        return;
    }

    if (m_darkSpaceUpdates.isEmpty() && m_darkSpaceUpdatesError.isEmpty()) {
        requestDarkSpaceUpdates();
        if (m_launchUpdatesStatus) {
            m_launchUpdatesStatus->setText("Loading...");
        }
        addLaunchInfoItem(m_launchUpdatesList, "Loading development log...", "Fetching the latest DarkSpace update notes.");
        return;
    }

    if (m_darkSpaceUpdates.isEmpty()) {
        if (m_launchUpdatesStatus) {
            m_launchUpdatesStatus->setText("Unavailable");
        }
        addLaunchInfoItem(
            m_launchUpdatesList,
            "Development log unavailable",
            m_darkSpaceUpdatesError.isEmpty() ? QString("Could not read the DarkSpace development log.") : m_darkSpaceUpdatesError,
            kDarkSpaceLogUrl);
        return;
    }

    int count = 0;
    for (const QJsonValue &value : m_darkSpaceUpdates) {
        const QJsonObject item = value.toObject();
        addLaunchInfoItem(
            m_launchUpdatesList,
            item.value("title").toString(),
            item.value("body").toString(),
            item.value("url").toString(kDarkSpaceLogUrl));
        ++count;
    }
    if (m_launchUpdatesStatus) {
        m_launchUpdatesStatus->setText(QString("%1 notes").arg(count));
    }
}

void MainWindow::requestDarkSpaceUpdates(bool force)
{
    if (m_darkSpaceUpdatesLoading) {
        return;
    }
    if (!force && !m_darkSpaceUpdates.isEmpty()) {
        return;
    }

    m_darkSpaceUpdatesLoading = true;
    m_darkSpaceUpdatesError.clear();
    if (m_launchUpdatesStatus) {
        m_launchUpdatesStatus->setText("Loading...");
    }

    auto *watcher = new QFutureWatcher<DarkSpaceUpdatesResult>(this);
    connect(watcher, &QFutureWatcher<DarkSpaceUpdatesResult>::finished, this, [this, watcher]() {
        const DarkSpaceUpdatesResult result = watcher->result();
        watcher->deleteLater();
        m_darkSpaceUpdatesLoading = false;
        m_darkSpaceUpdates = result.items;
        m_darkSpaceUpdatesError = result.error;
        populateDarkSpaceUpdatesPanel(selectedLaunchId());
    });
    watcher->setFuture(QtConcurrent::run([]() {
        return fetchDarkSpaceUpdates(QUrl(kDarkSpaceLogUrl));
    }));
}


