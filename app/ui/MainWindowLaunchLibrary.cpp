#include "ui/MainWindow.h"

#include "launcher/LaunchCatalog.h"
#include "ui/LaunchPresentation.h"
#include "ui/MainWindowLaunchSupport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QStatusBar>

#include <algorithm>

using namespace gamecq::launch_ui;
QString MainWindow::defaultLaunchSelectionId() const
{
    return "darkspace-d12-live";
}

void MainWindow::rebuildLaunchGroupFilter(const QString &preferredGroup)
{
    if (!m_launchGroupFilter) {
        return;
    }

    const QString current = preferredGroup.isEmpty() ? m_launchGroupFilter->currentData().toString() : preferredGroup;
    m_updatingLaunchList = true;
    m_launchGroupFilter->clear();
    m_launchGroupFilter->addItem("All", "all");
    for (const QString &group : launchLibraryGroups()) {
        m_launchGroupFilter->addItem(group, group);
    }
    m_launchGroupFilter->insertSeparator(m_launchGroupFilter->count());
    m_launchGroupFilter->addItem("New...", "__new__");

    const int index = m_launchGroupFilter->findData(current);
    m_launchGroupFilter->setCurrentIndex(index >= 0 ? index : 0);
    m_updatingLaunchList = false;
}

void MainWindow::saveLaunchLibraryControls() const
{
    QSettings settings;
    if (m_launchKindFilter) {
        settings.setValue("Launch/LibraryKindFilter", m_launchKindFilter->currentData().toString());
    }
    if (m_launchGroupFilter && m_launchGroupFilter->currentData().toString() != "__new__") {
        settings.setValue("Launch/LibraryGroupFilter", m_launchGroupFilter->currentData().toString());
    }
    if (m_launchSort) {
        settings.setValue("Launch/LibrarySort", m_launchSort->currentData().toString());
    }
}

void MainWindow::updateLaunchDragMode()
{
    if (!m_launchList) {
        return;
    }

    const bool customSort = !m_launchSort || m_launchSort->currentData().toString() == "custom";
    m_launchList->setDragDropMode(customSort ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
    m_launchList->setDragEnabled(customSort);
    m_launchList->setAcceptDrops(customSort);
    m_launchList->setDropIndicatorShown(customSort);
}

void MainWindow::handleLaunchListReordered()
{
    if (m_updatingLaunchList || !m_launchList || (m_launchSort && m_launchSort->currentData().toString() != "custom")) {
        return;
    }

    QStringList visibleIds;
    for (int row = 0; row < m_launchList->count(); ++row) {
        const QString id = m_launchList->item(row)->data(kLaunchIdRole).toString();
        if (!id.isEmpty()) {
            visibleIds.append(id);
        }
    }

    QStringList order = visibleIds;
    for (const QString &id : launchLibraryOrder()) {
        if (!containsCaseInsensitive(order, id)) {
            order.append(id);
        }
    }
    for (const LaunchEntry &entry : launchCatalog()) {
        if (!containsCaseInsensitive(order, entry.id)) {
            order.append(entry.id);
        }
    }
    saveLaunchLibraryOrder(order);
    statusBar()->showMessage("Library order saved.", 2000);
}

void MainWindow::createLaunchGroup()
{
    const QString previousGroup = settingString("Launch/LibraryGroupFilter", "all");
    bool accepted = false;
    const QString group = QInputDialog::getText(
        this,
        "New Library Group",
        "Group name",
        QLineEdit::Normal,
        {},
        &accepted)
                              .trimmed();

    if (!accepted || group.isEmpty()) {
        rebuildLaunchGroupFilter(previousGroup);
        return;
    }

    QStringList groups = savedLaunchLibraryGroups();
    addUniqueCaseInsensitive(groups, group);
    saveLaunchLibraryGroups(groups);
    rebuildLaunchGroupFilter(group);
    saveLaunchLibraryControls();
    populateLaunchList();
}

void MainWindow::populateLaunchList()
{
    if (m_launchList) {
        const QString previousLaunchId = selectedLaunchId();
        const QString desiredLaunchId = previousLaunchId.isEmpty() ? defaultLaunchSelectionId() : previousLaunchId;
        QListWidgetItem *desiredItem = nullptr;

        if (m_launchGroupFilter && !m_updatingLaunchList) {
            rebuildLaunchGroupFilter(m_launchGroupFilter->currentData().toString());
        }

        const QString kindFilter = m_launchKindFilter ? m_launchKindFilter->currentData().toString() : settingString("Launch/LibraryKindFilter", "all");
        const QString groupFilter = m_launchGroupFilter ? m_launchGroupFilter->currentData().toString() : settingString("Launch/LibraryGroupFilter", "all");
        const QString sortMode = m_launchSort ? m_launchSort->currentData().toString() : settingString("Launch/LibrarySort", "custom");
        const QStringList order = launchLibraryOrder();
        QList<LaunchEntry> catalog = launchCatalog();
        QHash<QString, int> catalogIndex;
        for (int i = 0; i < catalog.size(); ++i) {
            catalogIndex.insert(catalog.at(i).id, i);
        }

        QList<LaunchEntry> entries;
        for (const LaunchEntry &entry : catalog) {
            if (entry.staffOnly && !isStaffProfile()) {
                continue;
            }
            if (isFoldedDarkSpaceUtility(entry)) {
                continue;
            }
            const QString kind = launchKindForEntry(entry);
            if (kindFilter != "all" && kind != kindFilter) {
                continue;
            }
            const QString group = launchGroupForEntry(entry);
            if (groupFilter != "all" && group.compare(groupFilter, Qt::CaseInsensitive) != 0) {
                continue;
            }
            entries.append(entry);
        }

        std::stable_sort(entries.begin(), entries.end(), [&](const LaunchEntry &left, const LaunchEntry &right) {
            if (sortMode == "name") {
                return QString::localeAwareCompare(launchLibraryDisplayName(left), launchLibraryDisplayName(right)) < 0;
            }
            if (sortMode == "lastUsed") {
                const QDateTime leftUsed = launchLastUsed(left.id);
                const QDateTime rightUsed = launchLastUsed(right.id);
                if (leftUsed != rightUsed) {
                    return leftUsed > rightUsed;
                }
                return QString::localeAwareCompare(launchLibraryDisplayName(left), launchLibraryDisplayName(right)) < 0;
            }
            if (sortMode == "newest") {
                return catalogIndex.value(left.id, 0) > catalogIndex.value(right.id, 0);
            }
            if (sortMode == "group") {
                const int groupCompare = QString::localeAwareCompare(
                    launchGroupForEntry(left),
                    launchGroupForEntry(right));
                if (groupCompare != 0) {
                    return groupCompare < 0;
                }
                return QString::localeAwareCompare(launchLibraryDisplayName(left), launchLibraryDisplayName(right)) < 0;
            }

            const int leftOrder = launchOrderIndex(order, left.id);
            const int rightOrder = launchOrderIndex(order, right.id);
            if (leftOrder != rightOrder) {
                return leftOrder < rightOrder;
            }
            return catalogIndex.value(left.id, 0) < catalogIndex.value(right.id, 0);
        });

        m_updatingLaunchList = true;
        m_launchList->blockSignals(true);
        m_launchList->clear();
        for (const LaunchEntry &entry : entries) {
            auto *item = new QListWidgetItem(launchLibraryDisplayName(entry), m_launchList);
            const QIcon icon = launchListIcon(launchProgramIcon(entry));
            const QString statusText = launchStatusText(entry.id);
            const QString visibleStatusText = shouldShowLaunchStatusText(entry, statusText) ? statusText : QString();
            item->setIcon(icon);
            item->setData(kLaunchIdRole, entry.id);
            item->setData(kLaunchKindRole, launchKindForEntry(entry));
            item->setData(kLaunchGroupRole, launchGroupForEntry(entry));
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
            item->setSizeHint(QSize(0, 38));
            if (entry.id == desiredLaunchId) {
                desiredItem = item;
            }
        }

        if (!desiredItem && m_launchList->count() > 0) {
            desiredItem = m_launchList->item(0);
        }
        if (desiredItem) {
            m_launchList->setCurrentItem(desiredItem);
        }
        m_launchList->blockSignals(false);
        m_updatingLaunchList = false;
        updateLaunchDragMode();
        updateLaunchDetails();
        return;
    }

    return;
}

QString MainWindow::selectedLaunchId() const
{
    if (m_launchList) {
        if (QListWidgetItem *item = m_launchList->currentItem()) {
            return item->data(kLaunchIdRole).toString();
        }
        return {};
    }

    return {};
}

