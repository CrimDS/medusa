#include "ui/MainWindow.h"

#include "net/GameProtocolConstants.h"
#include "net/MetaClientBridge.h"
#include "net/ProfileFlags.h"
#include "ui/BrowserSupport.h"
#include "ui/ChatLogWriter.h"
#include "ui/LegacyIcons.h"
#include "ui/ProfilePresentation.h"
#include "ui/SideDrawerItems.h"
#include "ui/StatusLine.h"

#include <QAction>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QTabBar>
#include <QTextBrowser>
#include <QTimer>

#include <algorithm>
void MainWindow::onLoginSucceeded(const QString &displayName, quint32 flags, quint32 sessionId)
{
    persistPendingLogin();
    m_profileName = displayName;
    m_profileFlags = flags;
    m_sessionId = sessionId;
    m_statusLine->setProfile(displayName, flags);
    m_chatLogWriter->setAccountState(m_profileName, m_sessionId);
    applyProfileAccess();
    requestFriendsRefresh();
    if (m_tabBar && m_tabBar->currentIndex() == 1) {
        refreshServers();
    }
    if (m_chatLog || m_chatWebLog) {
        m_chatHtmlLines.clear();
        if (m_chatLog) {
            m_chatLog->clear();
        } else {
            refreshChatLogTheme();
        }
        loadRecentChatHistory();
        appendSystemChatLine(QString("Logged in as %1. Selecting lobby...").arg(displayName.toHtmlEscaped()));
    }
    if (shouldRefreshBrowserForSession(m_browserView != nullptr, m_browserCurrentUrl, m_sessionId)) {
        QTimer::singleShot(0, this, [this]() {
            navigateBrowser(browserHomeUrl(m_gameLinks, m_sessionId), false);
        });
    }
    statusBar()->showMessage(QString("Logged in as %1 - session %2").arg(displayName).arg(sessionId), 4000);
}

void MainWindow::onLoginFailed(int, const QString &message)
{
    const bool wasAutoLogin = m_autoLoginInProgress;
    clearPendingLogin();
    statusBar()->showMessage(message, 5000);
    if (wasAutoLogin) {
        QTimer::singleShot(0, this, &MainWindow::showLoginDialog);
    }
}

void MainWindow::onGameSelected(const QString &name, quint32 gameId)
{
    appendSystemChatLine(QString("Selected %1 lobby (@%2).").arg(name.toHtmlEscaped()).arg(gameId));
    statusBar()->showMessage(QString("Selected %1 lobby").arg(name), 3000);
}

void MainWindow::onGameLinksChanged(const GameLinks &links)
{
    m_gameLinks = links;
    if (m_browserView && isBrowserAtStartupOrHome(m_browserCurrentUrl)) {
        navigateBrowser(browserHomeUrl(m_gameLinks, m_sessionId), false);
    }
}

void MainWindow::onRoomJoined(quint32 roomId, const QString &name)
{
    appendSystemChatLine(QString("Joined %1 (@%2).").arg(name.toHtmlEscaped()).arg(roomId));
    statusBar()->showMessage(QString("Joined %1").arg(name), 3000);
}

void MainWindow::onProfileChanged(const QString &displayName, quint32 flags)
{
    m_profileName = displayName;
    m_profileFlags = flags;
    m_statusLine->setProfile(displayName, flags);
    m_chatLogWriter->setAccountState(m_profileName, m_sessionId);
    applyProfileAccess();
}

void MainWindow::onRoomsChanged(const QList<ChatRoomInfo> &rooms)
{
    if (m_roomsMenu) {
        m_roomsMenu->clear();
        auto *createRoom = m_roomsMenu->addAction("Create Room...");
        createRoom->setIcon(legacyIcon("room"));
        connect(createRoom, &QAction::triggered, this, &MainWindow::showCreateRoomDialog);
        m_roomsMenu->addSeparator();

        if (rooms.isEmpty()) {
            auto *empty = m_roomsMenu->addAction("No rooms returned");
            empty->setEnabled(false);
        } else {
            for (const ChatRoomInfo &room : rooms) {
                auto *action = m_roomsMenu->addAction(roomMenuLabel(room));
                action->setIcon(legacyIcon("room"));
                action->setData(room.roomId);
                connect(action, &QAction::triggered, this, [this, room]() {
                    QString password;
                    if ((room.flags & kRoomPasswordFlag) != 0) {
                        bool ok = false;
                        password = QInputDialog::getText(
                            this,
                            "Room Password",
                            QString("Password for %1").arg(room.name),
                            QLineEdit::Password,
                            {},
                            &ok);
                        if (!ok) {
                            return;
                        }
                    }

                    m_bridge->joinRoom(room.roomId, password, room.name);
                });
            }
        }

        m_roomsMenu->addSeparator();
        auto *refresh = m_roomsMenu->addAction("Refresh", this, [this]() {
            m_bridge->requestRooms();
        });
        refresh->setIcon(legacyIcon("activity"));
    }

    populateSideDrawerRooms(rooms);
}

void MainWindow::onChatMessagesReceived(const QList<ChatMessage> &messages)
{
    for (const ChatMessage &message : messages) {
        appendChatMessage(message);
    }
}

void MainWindow::onRoomMembersChanged(const QList<RoomMember> &members)
{
    if (!m_membersList || !m_membersCount) {
        return;
    }

    m_membersList->clear();
    m_membersCount->setText(QString::number(members.size()));

    QList<RoomMember> sortedMembers = members;
    std::stable_sort(sortedMembers.begin(), sortedMembers.end(), roomMemberLessThan);

    for (const RoomMember &member : sortedMembers) {
        auto *item = new QListWidgetItem(member.name, m_membersList);
        item->setData(kMemberUserIdRole, member.userId);
        item->setData(kMemberNameRole, member.name);
        item->setData(kMemberFlagsRole, member.flags);
        item->setData(kMemberStatusRole, member.status);
        if (!member.status.isEmpty()) {
            item->setToolTip(member.status);
        }
        applyProfileItemVisuals(item, member);
    }
}

void MainWindow::onFriendsChanged(const QList<RoomMember> &friends)
{
    m_friends = friends;
    m_friendsLoaded = true;
    m_friendsLoading = false;
    populateFriendsMenu();
    populateSideDrawerFriends();
}

void MainWindow::onFleetChanged(const QList<RoomMember> &members)
{
    populateSideDrawerProfiles("fleet", members, "No fleet members returned");

    if (m_fleetMenu) {
        m_fleetMenu->clear();
        if (members.isEmpty()) {
            auto *empty = m_fleetMenu->addAction("No fleet members returned");
            empty->setEnabled(false);
        } else {
            for (const RoomMember &member : members) {
                QString label = member.name;
                const QString suffix = memberSuffix(member.flags);
                if (!suffix.isEmpty()) {
                    label += QString("    %1").arg(suffix);
                }

                auto *action = m_fleetMenu->addAction(label);
                action->setIcon(makeProfileIcon(member.name, member.flags));
                action->setData(member.userId);
                if (!member.status.isEmpty()) {
                    action->setToolTip(member.status);
                }
                connect(action, &QAction::triggered, this, [this, member]() {
                    if (!m_chatInput) {
                        return;
                    }

                    m_chatInput->setText(QString("/send @%1 ").arg(member.userId));
                    m_chatInput->setFocus();
                    m_chatInput->setCursorPosition(m_chatInput->text().size());
                });
            }
        }

        m_fleetMenu->addSeparator();
        auto *fleetPage = m_fleetMenu->addAction("Fleet Page", this, [this]() {
            if (m_tabBar) {
                m_tabBar->setCurrentIndex(0);
            }
            navigateBrowser(QUrl("https://www.darkspace.net/index.php?lang=en&module=clans.php"));
        });
        fleetPage->setIcon(legacyIcon("clans"));
    }
}

void MainWindow::onStaffChanged(const QList<RoomMember> &staff)
{
    if (!isStaffProfile()) {
        return;
    }

    populateSideDrawerProfiles("staff", staff, "No staff online");

    if (m_staffMenu) {
        m_staffMenu->clear();
        if (staff.isEmpty()) {
            auto *empty = m_staffMenu->addAction("No staff online");
            empty->setEnabled(false);
        } else {
            for (const RoomMember &staffMember : staff) {
                QString role;
                if ((staffMember.flags & kAdministratorFlag) != 0) {
                    role = "[ADMIN] ";
                } else if ((staffMember.flags & kModeratorFlag) != 0) {
                    role = "[MOD] ";
                } else if ((staffMember.flags & kDeveloperFlag) != 0) {
                    role = "[DEV] ";
                }

                QString label = role + staffMember.name;
                if (!staffMember.status.isEmpty()) {
                    label += QString("    %1").arg(staffMember.status);
                }

                auto *action = m_staffMenu->addAction(label);
                action->setIcon(makeProfileIcon(staffMember.name, staffMember.flags));
                action->setData(staffMember.userId);
                connect(action, &QAction::triggered, this, [this, staffMember]() {
                    if (!m_chatInput) {
                        return;
                    }

                    m_chatInput->setText(QString("/send @%1 ").arg(staffMember.userId));
                    m_chatInput->setFocus();
                    m_chatInput->setCursorPosition(m_chatInput->text().size());
                });
            }
        }

        m_staffMenu->addSeparator();
        auto *watchList = m_staffMenu->addAction("Watch List");
        watchList->setIcon(legacyIcon("tool"));
        watchList->setEnabled(false);
    }
}

void MainWindow::onServersChanged(const QList<ServerInfo> &servers)
{
    m_servers = servers;
    if (m_launchServersRefreshButton) {
        m_launchServersRefreshButton->setEnabled(true);
    }
    updateLaunchDetails();

}


void MainWindow::wireBridge()
{
    connect(m_bridge, &MetaClientBridge::connectionStateChanged, m_statusLine, &StatusLine::setConnectionState);
    connect(m_bridge, &MetaClientBridge::loginSucceeded, this, &MainWindow::onLoginSucceeded);
    connect(m_bridge, &MetaClientBridge::loginFailed, this, &MainWindow::onLoginFailed);
    connect(m_bridge, &MetaClientBridge::gameSelected, this, &MainWindow::onGameSelected);
    connect(m_bridge, &MetaClientBridge::gameLinksChanged, this, &MainWindow::onGameLinksChanged);
    connect(m_bridge, &MetaClientBridge::roomJoined, this, &MainWindow::onRoomJoined);
    connect(m_bridge, &MetaClientBridge::profileChanged, this, &MainWindow::onProfileChanged);
    connect(m_bridge, &MetaClientBridge::roomsChanged, this, &MainWindow::onRoomsChanged);
    connect(m_bridge, &MetaClientBridge::chatMessagesReceived, this, &MainWindow::onChatMessagesReceived);
    connect(m_bridge, &MetaClientBridge::roomMembersChanged, this, &MainWindow::onRoomMembersChanged);
    connect(m_bridge, &MetaClientBridge::friendsChanged, this, &MainWindow::onFriendsChanged);
    connect(m_bridge, &MetaClientBridge::fleetChanged, this, &MainWindow::onFleetChanged);
    connect(m_bridge, &MetaClientBridge::staffChanged, this, &MainWindow::onStaffChanged);
    connect(m_bridge, &MetaClientBridge::serversChanged, this, &MainWindow::onServersChanged);
    connect(m_bridge, &MetaClientBridge::actionSucceeded, this, [this](const QString &context, const QString &message) {
        statusBar()->showMessage(message, 4000);
        appendSystemChatLine(QString("%1: %2").arg(context.toHtmlEscaped(), message.toHtmlEscaped()));
    });
    connect(m_bridge, &MetaClientBridge::errorOccurred, this, [this](const QString &context, const QString &message) {
        statusBar()->showMessage(QString("%1: %2").arg(context, message), 6000);
        if (context == "Servers") {
            if (m_launchServersRefreshButton) {
                m_launchServersRefreshButton->setEnabled(true);
            }
            if (m_launchServersStatus) {
                m_launchServersStatus->setText("Refresh failed");
            }
        }
        if (context == "MetaClient" || context == "Account" || context == "Chat" || context == "Rooms" || context == "Friends" || context == "Fleet" || context == "Find User" || context == "Ignored Users" || (context == "Staff" && isStaffProfile()) || context == "Servers") {
            appendSystemChatLine(QString("%1: %2").arg(context.toHtmlEscaped(), message.toHtmlEscaped()));
        }
    });
}

