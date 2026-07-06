#include "ui/MainWindow.h"

#include "net/MetaClientBridge.h"
#include "ui/EmoteStore.h"
#include "ui/LegacyIcons.h"
#include "ui/ProfilePresentation.h"
#include "ui/SideDrawerItems.h"
#include "ui/UserDialogs.h"

#include <QAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPoint>
#include <QStatusBar>

void MainWindow::requestFriendsRefresh()
{
    if (m_profileName.isEmpty() || m_friendsLoading) {
        return;
    }

    m_friendsLoading = true;
    m_bridge->requestFriends();
    populateFriendsMenu();
    populateSideDrawerFriends();
}

void MainWindow::populateFriendsMenu()
{
    if (!m_friendsMenu) {
        return;
    }

    m_friendsMenu->clear();
    if (m_friendsLoading && !m_friendsLoaded) {
        auto *loading = m_friendsMenu->addAction("Loading friends...");
        loading->setIcon(legacyIcon("activity"));
        loading->setEnabled(false);
    } else if (!m_friendsLoaded) {
        auto *notLoaded = m_friendsMenu->addAction("Friends not loaded yet");
        notLoaded->setIcon(legacyIcon("friends"));
        notLoaded->setEnabled(false);
    } else if (m_friends.isEmpty()) {
        auto *empty = m_friendsMenu->addAction("No friends returned");
        empty->setIcon(legacyIcon("friends"));
        empty->setEnabled(false);
    } else {
        for (const RoomMember &friendProfile : sortedFriends(m_friends)) {
            QString label = friendProfile.name;
            const QString suffix = memberSuffix(friendProfile.flags);
            if (!suffix.isEmpty()) {
                label += QString("    %1").arg(suffix);
            }

            auto *action = m_friendsMenu->addAction(label);
            action->setIcon(makeProfileIcon(friendProfile.name, friendProfile.flags));
            action->setData(friendProfile.userId);
            if (!friendProfile.status.isEmpty()) {
                action->setToolTip(friendProfile.status);
            }
            connect(action, &QAction::triggered, this, [this, friendProfile]() {
                setChatCommand(QString("/send @%1 ").arg(friendProfile.userId));
            });
        }
    }

    if (m_friendsLoading && m_friendsLoaded) {
        m_friendsMenu->addSeparator();
        auto *refreshing = m_friendsMenu->addAction("Refreshing...");
        refreshing->setIcon(legacyIcon("activity"));
        refreshing->setEnabled(false);
    }

    m_friendsMenu->addSeparator();
    auto *refresh = m_friendsMenu->addAction("Refresh", this, [this]() {
        requestFriendsRefresh();
    });
    refresh->setIcon(legacyIcon("activity"));
    auto *manage = m_friendsMenu->addAction("Manage Friends", this, &MainWindow::showFriendsDialog);
    manage->setIcon(legacyIcon("friends"));
}


UserDialogsCallbacks MainWindow::userDialogsCallbacks()
{
    UserDialogsCallbacks callbacks;
    callbacks.setChatCommand = [this](const QString &command) {
        setChatCommand(command);
    };
    callbacks.openProfile = [this](quint32 userId, const QString &name) {
        openProfile(userId, name);
    };
    callbacks.setFriendsLoading = [this](bool loading) {
        m_friendsLoading = loading;
    };
    callbacks.refreshFriendsUi = [this]() {
        populateFriendsMenu();
        populateSideDrawerFriends();
    };
    callbacks.currentFriends = [this]() {
        return m_friends;
    };
    return callbacks;
}

void MainWindow::showFindUserDialog()
{
    ::showFindUserDialog(this, m_bridge, userDialogsCallbacks());
}

void MainWindow::showFriendsDialog()
{
    ::showFriendsDialog(this, m_bridge, m_profileName, m_friendsLoaded, m_friends, userDialogsCallbacks());
}

void MainWindow::showIgnoredUsersDialog()
{
    ::showIgnoredUsersDialog(this, m_bridge);
}
void MainWindow::showMemberContextMenu(const QPoint &position)
{
    showProfileContextMenu(m_membersList, position);
}

void MainWindow::showProfileContextMenu(QListWidget *list, const QPoint &position)
{
    if (!list) {
        return;
    }

    QListWidgetItem *item = list->itemAt(position);
    if (!item) {
        return;
    }

    const quint32 userId = item->data(kMemberUserIdRole).toUInt();
    const QString name = item->data(kMemberNameRole).toString();
    const quint32 flags = item->data(kMemberFlagsRole).toUInt();
    if (userId == 0 || name.isEmpty()) {
        return;
    }

    QMenu menu(this);
    auto *title = menu.addAction(name);
    title->setIcon(makeProfileIcon(name, flags));
    title->setEnabled(false);
    menu.addSeparator();

    auto *privateMessage = menu.addAction("Private Message", this, [this, userId]() {
        setChatCommand(QString("/send @%1 ").arg(userId));
    });
    privateMessage->setIcon(legacyIcon("friend"));
    auto *viewProfile = menu.addAction("View Profile", this, [this, userId, name]() {
        openProfile(userId, name);
    });
    viewProfile->setIcon(legacyIcon("Avatar"));
    QMenu *emoteMenu = menu.addMenu("Emotes");
    emoteMenu->setIcon(legacyIcon("activity"));
    const QList<EmoteEntry> emotes = EmoteStore::loadEmotes();
    if (emotes.isEmpty()) {
        auto *empty = emoteMenu->addAction("No emotes configured");
        empty->setIcon(legacyIcon("activity"));
        empty->setEnabled(false);
    } else {
        for (const EmoteEntry &emote : emotes) {
            auto *action = emoteMenu->addAction(emote.name);
            action->setIcon(legacyIcon("activity"));
            action->setToolTip(emote.text);
            connect(action, &QAction::triggered, this, [this, emote, name]() {
                const QString sender = m_profileName.isEmpty() ? QString("Me") : m_profileName;
                const QString text = EmoteStore::renderEmoteText(emote, sender, name);
                if (!text.isEmpty()) {
                    m_bridge->sendChatMessage(text);
                }
            });
        }
    }
    emoteMenu->addSeparator();
    auto *editEmotes = emoteMenu->addAction("Edit Emotes...", this, &MainWindow::showOptionsDialog);
    editEmotes->setIcon(legacyIcon("tool"));
    menu.addSeparator();
    auto *addFriend = menu.addAction("Add Friend", this, [this, userId, name]() {
        statusBar()->showMessage(QString("Adding %1 to friends...").arg(name), 3000);
        m_bridge->addFriend(userId, name);
    });
    addFriend->setIcon(legacyIcon("friend"));
    auto *ignore = menu.addAction("Ignore", this, [this, userId, name]() {
        statusBar()->showMessage(QString("Ignoring %1...").arg(name), 3000);
        m_bridge->addIgnore(userId, name);
    });

    if (isModeratorProfile()) {
        menu.addSeparator();
        auto *moderation = menu.addAction("Moderation");
        moderation->setIcon(legacyIcon("tool"));
        moderation->setEnabled(false);
        auto *check = menu.addAction("Check Record", this, [this, userId]() {
            setChatCommand(QString("/check @%1").arg(userId));
        });
        check->setIcon(legacyIcon("tool"));
        auto *watch = menu.addAction("Watch", this, [this, userId]() {
            setChatCommand(QString("/watch @%1 ").arg(userId));
        });
        watch->setIcon(legacyIcon("tool"));
        auto *mute = menu.addAction("Mute", this, [this, userId]() {
            setChatCommand(QString("/mute @%1").arg(userId));
        });
        mute->setIcon(legacyIcon("cancel"));
        auto *kick = menu.addAction("Kick", this, [this, userId]() {
            setChatCommand(QString("/kick @%1 ").arg(userId));
        });
        kick->setIcon(legacyIcon("cancel"));
        auto *ban = menu.addAction("Ban", this, [this, userId]() {
            setChatCommand(QString("/ban @%1 ").arg(userId));
        });
        ban->setIcon(legacyIcon("cancel"));
        auto *clones = menu.addAction("Find Clones", this, [this, userId]() {
            setChatCommand(QString("/clones @%1").arg(userId));
        });
        clones->setIcon(legacyIcon("Avatar"));
    }

    menu.exec(list->viewport()->mapToGlobal(position));
}


void MainWindow::showCreateRoomDialog()
{
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this,
        "Create Room",
        "Room name",
        QLineEdit::Normal,
        {},
        &accepted);

    if (!accepted || name.trimmed().isEmpty()) {
        return;
    }

    const QString password = QInputDialog::getText(
        this,
        "Create Room",
        "Password (optional)",
        QLineEdit::Password,
        {},
        &accepted);

    if (!accepted) {
        return;
    }

    m_bridge->createRoom(name, password);
}

