#include "ui/MainWindow.h"

#include "launcher/LaunchCatalog.h"
#include "launcher/HttpManifestUpdater.h"
#include "launcher/LaunchMetadata.h"
#include "ui/LaunchInfoList.h"
#include "ui/LaunchPresentation.h"
#include "ui/LaunchWidgets.h"
#include "ui/MainWindowLaunchSupport.h"

#include <QAction>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QProcess>
#include <QPushButton>
#include <QStatusBar>
#include <QUrl>
#include <QtConcurrent>

using namespace gamecq::launch_ui;
QString MainWindow::launchStatusText(const QString &launchId) const
{
    const LaunchEntry entry = findLaunchEntry(launchId);
    if (!hasLaunchEntry(entry)) {
        return {};
    }

    if (launchEntrySharesCurrentUpdate(launchId) && !m_launchUpdateStatus.isEmpty()) {
        return launchId == m_launchUpdateId ? m_launchUpdateStatus : "Shared update running";
    }

    return launchStatus(entry);
}

bool MainWindow::launchEntrySharesCurrentUpdate(const QString &launchId) const
{
    const LaunchEntry entry = findLaunchEntry(launchId);
    const LaunchEntry updating = findLaunchEntry(m_launchUpdateId);
    if (!hasLaunchEntry(entry) || !hasLaunchEntry(updating)) {
        return false;
    }

    const bool sharedD12Update = entry.httpManifestUpdater && updating.httpManifestUpdater;
    const bool sharedLegacyUpdate = usesLegacyMirrorUpdater(entry) && usesLegacyMirrorUpdater(updating);
    return (sharedD12Update || sharedLegacyUpdate)
           && QDir::cleanPath(entry.workingDirectory).compare(QDir::cleanPath(updating.workingDirectory), Qt::CaseInsensitive) == 0;
}

void MainWindow::refreshLaunchStatus()
{
    if (m_launchCancelAction) {
        const bool legacyRunning = m_legacyMirrorProcess && m_legacyMirrorProcess->state() != QProcess::NotRunning;
        m_launchCancelAction->setEnabled((m_d12Updater && m_d12Updater->isRunning()) || legacyRunning);
    }
    if (m_launchList) {
        for (int row = 0; row < m_launchList->count(); ++row) {
            QListWidgetItem *item = m_launchList->item(row);
            if (!item) {
                continue;
            }

            const QString launchId = item->data(kLaunchIdRole).toString();
            const LaunchEntry entry = findLaunchEntry(launchId);
            if (!hasLaunchEntry(entry)) {
                continue;
            }

            const QString statusText = launchStatusText(launchId);
            const QString visibleStatusText = shouldShowLaunchStatusText(entry, statusText) ? statusText : QString();
            item->setText(launchLibraryDisplayName(entry));
            QStringList tooltipParts;
            const QString subtitle = launchSubtitleText(entry);
            if (!subtitle.isEmpty()) {
                tooltipParts << subtitle;
            }
            if (!visibleStatusText.isEmpty()) {
                tooltipParts << visibleStatusText;
            }
            tooltipParts << QDir::toNativeSeparators(resolveLaunchPath(entry, entry.executable));
            item->setToolTip(tooltipParts.join('\n'));
            const QIcon icon = launchListIcon(launchProgramIcon(entry));
            item->setIcon(icon);
        }
        updateLaunchDetails();
        return;
    }


}

void MainWindow::updateLaunchDetails()
{
    const QString launchId = selectedLaunchId();
    const LaunchEntry entry = findLaunchEntry(launchId);
    const bool hasEntry = hasLaunchEntry(entry);

    if (!hasEntry) {
        if (m_launchArt) {
            if (auto *artLabel = dynamic_cast<LaunchArtLabel *>(m_launchArt)) {
                artLabel->setLaunchArt({}, "Select a Game or App");
            } else {
                m_launchArt->setPixmap({});
                m_launchArt->setText("Select a Game or App");
            }
            m_launchArt->setProperty("hasArt", false);
            polishObjectName(m_launchArt);
        }
        if (m_launchTitle) {
            m_launchTitle->setText("Nothing selected");
        }
        if (m_launchSubtitle) {
            m_launchSubtitle->clear();
        }
        if (m_launchDescription) {
            m_launchDescription->clear();
        }
        if (m_launchInstallPath) {
            m_launchInstallPath->clear();
            m_launchInstallPath->setVisible(false);
        }
        if (m_launchStatusPill) {
            m_launchStatusPill->clear();
            m_launchStatusPill->setVisible(false);
        }
        if (m_launchPlayButton) {
            m_launchPlayButton->setEnabled(false);
        }
        if (m_launchRepairButton) {
            m_launchRepairButton->setVisible(false);
        }
        if (m_launchSetupButton) {
            m_launchSetupButton->setVisible(false);
        }
        if (m_launchTutorialButton) {
            m_launchTutorialButton->setVisible(false);
        }
        if (m_launchMoreButton) {
            m_launchMoreButton->setEnabled(true);
        }
        if (m_launchDetailPanel) {
            if (auto *feature = dynamic_cast<LaunchFeatureFrame *>(m_launchDetailPanel)) {
                feature->setBackgroundArt({});
            }
            m_launchDetailPanel->setProperty("hasArt", false);
            polishObjectName(m_launchDetailPanel);
        }
        populateLaunchServerList({});
        return;
    }

    const QString statusText = launchStatusText(launchId);
    const QString executable = resolveLaunchPath(entry, entry.executable);
    const bool installed = QFileInfo::exists(executable);
    const bool canUpdate = entry.httpManifestUpdater || usesLegacyMirrorUpdater(entry);
    const bool legacyRunning = m_legacyMirrorProcess && m_legacyMirrorProcess->state() != QProcess::NotRunning;
    const bool updateRunning = (m_d12Updater && m_d12Updater->isRunning()) || legacyRunning;
    const bool sharesUpdate = launchEntrySharesCurrentUpdate(entry.id);
    const LaunchEntry setupEntry = findLaunchEntry(relatedDarkSpaceLaunchId(entry, "setup"));
    const LaunchEntry tutorialEntry = findLaunchEntry(relatedDarkSpaceLaunchId(entry, "tutorial"));
    const bool canUseRelatedActions = !entry.stubOnly && (!updateRunning || !sharesUpdate);

    setLaunchFeatureBackground(m_launchDetailPanel, entry);
    setLaunchCapsuleArtwork(m_launchArt, entry);
    if (m_launchTitle) {
        m_launchTitle->setText(entry.name);
    }
    if (m_launchSubtitle) {
        m_launchSubtitle->setText(launchSubtitleText(entry));
    }
    if (m_launchDescription) {
        m_launchDescription->setText(launchDescriptionForDisplay(entry));
    }
    if (m_launchInstallPath) {
        m_launchInstallPath->clear();
        m_launchInstallPath->setToolTip(QString());
        m_launchInstallPath->setVisible(false);
    }
    const bool showStatusText = shouldShowLaunchStatusText(entry, statusText);
    if (m_launchStatusPill) {
        m_launchStatusPill->setVisible(showStatusText);
        if (showStatusText) {
            setPillLabel(
                m_launchStatusPill,
                statusText,
                launchStatusPillName(statusText),
                sharesUpdate ? m_launchUpdateDetail : QString());
        } else {
            m_launchStatusPill->clear();
            m_launchStatusPill->setToolTip(QString());
        }
    }

    if (m_launchPlayButton) {
        QString playText = "Launch";
        if (entry.stubOnly) {
            playText = "Stubbed";
        } else if (entry.needsServer) {
            playText = "Select Server";
        } else if (!installed && canUpdate) {
            playText = "Install";
        } else if (entry.name.contains("DarkSpace", Qt::CaseInsensitive)) {
            playText = "Play";
        }
        m_launchPlayButton->setText(playText);
        m_launchPlayButton->setEnabled(canUseRelatedActions);
    }

    if (m_launchRepairButton) {
        m_launchRepairButton->setVisible(canUpdate);
        m_launchRepairButton->setText(installed ? "Repair" : "Install");
        m_launchRepairButton->setEnabled(canUpdate && !entry.stubOnly && (!updateRunning || sharesUpdate));
    }

    if (m_launchSetupButton) {
        const bool showSetup = hasLaunchEntry(setupEntry) && (!setupEntry.staffOnly || isStaffProfile());
        m_launchSetupButton->setVisible(showSetup);
        m_launchSetupButton->setEnabled(showSetup && !setupEntry.stubOnly && canUseRelatedActions);
    }

    if (m_launchTutorialButton) {
        const bool showTutorial = hasLaunchEntry(tutorialEntry) && (!tutorialEntry.staffOnly || isStaffProfile());
        m_launchTutorialButton->setVisible(showTutorial);
        m_launchTutorialButton->setEnabled(showTutorial && !tutorialEntry.stubOnly && canUseRelatedActions);
    }

    if (m_launchMoreButton) {
        m_launchMoreButton->setEnabled(true);
    }

    populateLaunchServerList(launchId);
}


