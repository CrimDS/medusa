#include "ui/MainWindow.h"

#include "net/GameProtocolConstants.h"
#include "net/MetaClientBridge.h"
#include "ui/EmoteStore.h"
#include "ui/LegacyIcons.h"
#include "ui/ProfilePresentation.h"
#include "ui/SideDrawerItems.h"
#include "ui/UserDialogs.h"

#include <QAction>
#include <QInputDialog>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPoint>
#include <QRect>
#include <QSettings>
#include <QSize>
#include <QStatusBar>
#include <QToolButton>
#include <QtGlobal>

void MainWindow::showSideDrawer(const QString &mode, const QString &title, const QString &loadingText)
{
    if (!m_sideDrawer || !m_sideDrawerTitle || !m_sideDrawerList) {
        return;
    }

    m_sideDrawerMode = mode;
    m_sideDrawerTitle->setText(title);
    m_sideDrawerList->clear();
    if (!loadingText.isEmpty()) {
        addSideDrawerStatusItem(m_sideDrawerList, loadingText);
    }
    updateSideDrawerActions();

    auto *anchor = mode == "rooms" ? m_roomsButton
                 : mode == "friends" ? m_friendsButton
                 : mode == "fleet" ? m_fleetButton
                 : mode == "staff" ? m_staffButton
                 : nullptr;
    QWidget *surface = m_sideDrawer->parentWidget();
    if (anchor && surface) {
        constexpr int margin = 8;
        const int availableWidth = qMax(220, surface->width() - margin * 2);
        const int availableHeight = qMax(160, surface->height() - margin * 2);
        const QSize drawerSize(qMin(340, availableWidth), qMin(320, availableHeight));
        const QPoint anchorTopLeft = anchor->mapTo(surface, QPoint(0, 0));
        const int anchorCenterX = anchorTopLeft.x() + anchor->width() / 2;
        int x = anchorCenterX - drawerSize.width() / 2;
        int y = anchorTopLeft.y() - drawerSize.height() - 6;
        if (y < margin) {
            y = anchorTopLeft.y() + anchor->height() + 6;
        }
        x = qMax(margin, qMin(x, surface->width() - drawerSize.width() - margin));
        y = qMax(margin, qMin(y, surface->height() - drawerSize.height() - margin));
        m_sideDrawer->setGeometry(QRect(QPoint(x, y), drawerSize));
    }

    m_sideDrawer->setVisible(true);
    m_sideDrawer->raise();
    setSideDrawerButtonsChecked(mode);
}

void MainWindow::hideSideDrawer()
{
    if (m_sideDrawer) {
        m_sideDrawer->setVisible(false);
    }
    m_sideDrawerMode.clear();
    setSideDrawerButtonsChecked({});
}

void MainWindow::setSideDrawerButtonsChecked(const QString &mode)
{
    if (m_roomsButton) {
        m_roomsButton->setChecked(mode == "rooms");
    }
    if (m_friendsButton) {
        m_friendsButton->setChecked(mode == "friends");
    }
    if (m_fleetButton) {
        m_fleetButton->setChecked(mode == "fleet");
    }
    if (m_staffButton) {
        m_staffButton->setChecked(mode == "staff");
    }
}

void MainWindow::updateSideDrawerActions()
{
    if (!m_sideDrawerPrimaryButton || !m_sideDrawerRefreshButton) {
        return;
    }

    m_sideDrawerPrimaryButton->setVisible(false);
    m_sideDrawerRefreshButton->setVisible(false);
    m_sideDrawerRefreshButton->setText("Refresh");
    m_sideDrawerRefreshButton->setIcon(legacyIcon("activity"));
    m_sideDrawerRefreshButton->setToolTip("Refresh list");
    if (QMenu *oldMenu = m_sideDrawerRefreshButton->menu()) {
        m_sideDrawerRefreshButton->setMenu(nullptr);
        oldMenu->deleteLater();
    }
    m_sideDrawerRefreshButton->setPopupMode(QToolButton::DelayedPopup);

    if (m_sideDrawerMode == "rooms") {
        m_sideDrawerPrimaryButton->setVisible(true);
        m_sideDrawerPrimaryButton->setText("Create");
        m_sideDrawerPrimaryButton->setIcon(QIcon());
        m_sideDrawerPrimaryButton->setToolTip("Create room");
    } else if (m_sideDrawerMode == "friends") {
        m_sideDrawerPrimaryButton->setVisible(true);
        m_sideDrawerPrimaryButton->setText("Manage");
        m_sideDrawerPrimaryButton->setIcon(QIcon());
        m_sideDrawerPrimaryButton->setToolTip("Manage friends");
        m_sideDrawerRefreshButton->setVisible(true);
        m_sideDrawerRefreshButton->setText(QString("Sort: %1").arg(friendSortLabel(configuredFriendSortMode())));
        m_sideDrawerRefreshButton->setIcon(QIcon());
        m_sideDrawerRefreshButton->setToolTip("Sort friends");
        auto *sortMenu = new QMenu(m_sideDrawerRefreshButton);
        auto addSortAction = [this, sortMenu](const QString &mode, const QString &label) {
            QAction *action = sortMenu->addAction(label);
            action->setCheckable(true);
            action->setChecked(configuredFriendSortMode() == mode);
            connect(action, &QAction::triggered, this, [this, mode]() {
                QSettings().setValue("Friends/SortMode", mode);
                populateFriendsMenu();
                populateSideDrawerFriends();
            });
        };
        addSortAction("status", "Status");
        addSortAction("name", "Name");
        addSortAction("role", "Role");
        m_sideDrawerRefreshButton->setMenu(sortMenu);
        m_sideDrawerRefreshButton->setPopupMode(QToolButton::InstantPopup);
    } else if (m_sideDrawerMode == "fleet") {
        m_sideDrawerPrimaryButton->setVisible(true);
        m_sideDrawerPrimaryButton->setText("Fleet Page");
        m_sideDrawerPrimaryButton->setIcon(QIcon());
        m_sideDrawerPrimaryButton->setToolTip("Open the DarkSpace fleet page");
    }
}

void MainWindow::populateSideDrawerRooms(const QList<ChatRoomInfo> &rooms)
{
    if (!m_sideDrawerList || m_sideDrawerMode != "rooms") {
        return;
    }

    m_sideDrawerTitle->setText("Rooms");
    updateSideDrawerActions();
    m_sideDrawerList->clear();

    if (rooms.isEmpty()) {
        addSideDrawerStatusItem(m_sideDrawerList, "No rooms returned");
    } else {
        for (const ChatRoomInfo &room : rooms) {
            addSideDrawerRoomItem(m_sideDrawerList, room);
        }
    }
}

void MainWindow::populateSideDrawerProfiles(const QString &mode, const QList<RoomMember> &members, const QString &emptyText)
{
    if (!m_sideDrawerList || m_sideDrawerMode != mode) {
        return;
    }

    m_sideDrawerList->clear();
    updateSideDrawerActions();
    if (members.isEmpty()) {
        addSideDrawerStatusItem(m_sideDrawerList, emptyText);
    } else {
        for (const RoomMember &member : members) {
            addSideDrawerProfileItem(m_sideDrawerList, member);
        }
    }

}

void MainWindow::populateSideDrawerFriends()
{
    if (!m_sideDrawerList || m_sideDrawerMode != "friends") {
        return;
    }

    m_sideDrawerTitle->setText("Friends");
    updateSideDrawerActions();
    m_sideDrawerList->clear();
    if (m_friendsLoading && !m_friendsLoaded) {
        addSideDrawerStatusItem(m_sideDrawerList, "Loading friends...");
    } else if (!m_friendsLoaded) {
        addSideDrawerStatusItem(m_sideDrawerList, "Friends not loaded yet");
    } else if (m_friends.isEmpty()) {
        addSideDrawerStatusItem(m_sideDrawerList, "No friends returned");
    } else {
        for (const RoomMember &friendProfile : sortedFriends(m_friends)) {
            addSideDrawerProfileItem(m_sideDrawerList, friendProfile);
        }
    }

    if (m_friendsLoading && m_friendsLoaded) {
        addSideDrawerStatusItem(m_sideDrawerList, "Refreshing...");
    }
}

void MainWindow::handleSideDrawerItem(QListWidgetItem *item)
{
    if (!item || !item->flags().testFlag(Qt::ItemIsEnabled)) {
        return;
    }

    const QString kind = item->data(kSideItemKindRole).toString();
    if (kind == "room") {
        const QString name = item->data(kSideRoomNameRole).toString();
        const quint32 flags = item->data(kSideRoomFlagsRole).toUInt();
        QString password;
        if ((flags & kRoomPasswordFlag) != 0) {
            bool ok = false;
            password = QInputDialog::getText(this, "Room Password", QString("Password for %1").arg(name), QLineEdit::Password, {}, &ok);
            if (!ok) {
                return;
            }
        }
        m_bridge->joinRoom(item->data(kSideRoomIdRole).toUInt(), password, name);
        hideSideDrawer();
    } else if (kind == "profile") {
        setChatCommand(QString("/send @%1 ").arg(item->data(kMemberUserIdRole).toUInt()));
        hideSideDrawer();
    } else if (kind == "createRoom") {
        hideSideDrawer();
        showCreateRoomDialog();
    } else if (kind == "refreshRooms") {
        showSideDrawer("rooms", "Rooms", "Loading rooms...");
        m_bridge->requestRooms();
    } else if (kind == "refreshFriends") {
        populateSideDrawerFriends();
        requestFriendsRefresh();
    } else if (kind == "manageFriends") {
        hideSideDrawer();
        showFriendsDialog();
    } else if (kind == "refreshFleet") {
        showSideDrawer("fleet", "Fleet", "Loading fleet...");
        m_bridge->requestFleet();
    } else if (kind == "refreshStaff") {
        showSideDrawer("staff", "Staff", "Loading staff...");
        m_bridge->requestStaff();
    }
}

