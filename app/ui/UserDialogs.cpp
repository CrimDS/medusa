#include "ui/UserDialogs.h"

#include "net/MetaClientBridge.h"
#include "ui/LegacyIcons.h"
#include "ui/ProfilePresentation.h"

#include <QAbstractItemView>
#include <QAction>
#include <QDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

RoomMember selectedProfileFrom(QListWidget *list)
{
    RoomMember selected;
    QListWidgetItem *item = list ? list->currentItem() : nullptr;
    if (!item) {
        return selected;
    }

    selected.userId = item->data(kMemberUserIdRole).toUInt();
    selected.name = item->data(kMemberNameRole).toString();
    selected.flags = item->data(kMemberFlagsRole).toUInt();
    selected.status = item->data(kMemberStatusRole).toString();
    return selected;
}

void sendPrivateMessage(const UserDialogsCallbacks &callbacks, quint32 userId)
{
    if (userId != 0 && callbacks.setChatCommand) {
        callbacks.setChatCommand(QString("/send @%1 ").arg(userId));
    }
}

void openProfile(const UserDialogsCallbacks &callbacks, quint32 userId, const QString &name)
{
    if (userId != 0 && callbacks.openProfile) {
        callbacks.openProfile(userId, name);
    }
}

void markFriendsLoading(const UserDialogsCallbacks &callbacks, bool loading)
{
    if (callbacks.setFriendsLoading) {
        callbacks.setFriendsLoading(loading);
    }
}

void refreshFriendsUi(const UserDialogsCallbacks &callbacks)
{
    if (callbacks.refreshFriendsUi) {
        callbacks.refreshFriendsUi();
    }
}

QList<RoomMember> currentFriends(const UserDialogsCallbacks &callbacks)
{
    return callbacks.currentFriends ? callbacks.currentFriends() : QList<RoomMember>{};
}

} // namespace

void showFindUserDialog(QWidget *parent, MetaClientBridge *bridge, const UserDialogsCallbacks &callbacks)
{
    if (!bridge) {
        return;
    }

    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Find User");
    dialog->resize(520, 360);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *searchRow = new QHBoxLayout;
    auto *query = new QLineEdit(dialog);
    query->setPlaceholderText("User name or partial name...");
    auto *search = new QPushButton("Find", dialog);
    searchRow->addWidget(query, 1);
    searchRow->addWidget(search);
    layout->addLayout(searchRow);

    auto *status = new QLabel("Enter a name to search the meta-server.", dialog);
    status->setObjectName("sectionLabel");
    layout->addWidget(status);

    auto *results = new QListWidget(dialog);
    results->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(results, 1);

    auto *actions = new QHBoxLayout;
    auto *message = new QPushButton("Message", dialog);
    auto *profile = new QPushButton("Profile", dialog);
    auto *addFriend = new QPushButton("Add Friend", dialog);
    auto *ignore = new QPushButton("Ignore", dialog);
    auto *close = new QPushButton("Close", dialog);
    actions->addWidget(message);
    actions->addWidget(profile);
    actions->addWidget(addFriend);
    actions->addWidget(ignore);
    actions->addStretch(1);
    actions->addWidget(close);
    layout->addLayout(actions);

    auto selectedProfile = [results]() {
        return selectedProfileFrom(results);
    };

    auto runSearch = [bridge, query, results, status]() {
        const QString term = query->text().trimmed();
        results->clear();
        if (term.isEmpty()) {
            status->setText("Enter a name to search the meta-server.");
            return;
        }

        status->setText(QString("Searching for %1...").arg(term));
        bridge->findProfiles(term);
    };

    QObject::connect(search, &QPushButton::clicked, dialog, runSearch);
    QObject::connect(query, &QLineEdit::returnPressed, dialog, runSearch);
    QObject::connect(results, &QListWidget::itemDoubleClicked, dialog, [callbacks, selectedProfile](QListWidgetItem *) {
        sendPrivateMessage(callbacks, selectedProfile().userId);
    });
    QObject::connect(message, &QPushButton::clicked, dialog, [callbacks, selectedProfile]() {
        sendPrivateMessage(callbacks, selectedProfile().userId);
    });
    QObject::connect(profile, &QPushButton::clicked, dialog, [callbacks, selectedProfile]() {
        const RoomMember selected = selectedProfile();
        openProfile(callbacks, selected.userId, selected.name);
    });
    QObject::connect(addFriend, &QPushButton::clicked, dialog, [bridge, selectedProfile]() {
        const RoomMember selected = selectedProfile();
        if (selected.userId != 0) {
            bridge->addFriend(selected.userId, selected.name);
        }
    });
    QObject::connect(ignore, &QPushButton::clicked, dialog, [bridge, selectedProfile]() {
        const RoomMember selected = selectedProfile();
        if (selected.userId != 0) {
            bridge->addIgnore(selected.userId, selected.name);
        }
    });
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);

    QObject::connect(bridge, &MetaClientBridge::profilesFound, dialog, [query, results, status](const QString &term, const QList<RoomMember> &profiles) {
        if (term.compare(query->text().trimmed(), Qt::CaseInsensitive) != 0) {
            return;
        }

        results->clear();
        for (const RoomMember &profile : profiles) {
            addProfileListItem(results, profile);
        }

        status->setText(QString("%1 result%2 for %3.")
                            .arg(profiles.size())
                            .arg(profiles.size() == 1 ? "" : "s")
                            .arg(term));
    });
    QObject::connect(bridge, &MetaClientBridge::errorOccurred, dialog, [status](const QString &context, const QString &message) {
        if (context == "Find User") {
            status->setText(message);
        }
    });

    dialog->open();
    query->setFocus();
}

void showFriendsDialog(QWidget *parent,
                       MetaClientBridge *bridge,
                       const QString &profileName,
                       bool friendsLoaded,
                       const QList<RoomMember> &friends,
                       const UserDialogsCallbacks &callbacks)
{
    if (!bridge) {
        return;
    }

    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Friends");
    dialog->setWindowIcon(legacyIcon("friends"));
    dialog->resize(480, 360);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *status = new QLabel("Loading friends...", dialog);
    status->setObjectName("sectionLabel");
    layout->addWidget(status);

    auto *list = new QListWidget(dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list, 1);

    auto *actions = new QHBoxLayout;
    auto *message = new QPushButton("Message", dialog);
    auto *profile = new QPushButton("Profile", dialog);
    auto *remove = new QPushButton("Remove", dialog);
    auto *sortButton = new QToolButton(dialog);
    auto *refresh = new QPushButton("Refresh", dialog);
    auto *close = new QPushButton("Close", dialog);
    message->setIcon(legacyIcon("friend"));
    profile->setIcon(legacyIcon("Avatar"));
    remove->setIcon(legacyIcon("ico00002"));
    sortButton->setObjectName("sidePanelButton");
    sortButton->setIcon(legacyIcon("sort"));
    sortButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sortButton->setPopupMode(QToolButton::InstantPopup);
    refresh->setIcon(legacyIcon("activity"));
    close->setIcon(legacyIcon("cancel"));
    actions->addWidget(message);
    actions->addWidget(profile);
    actions->addWidget(remove);
    actions->addWidget(sortButton);
    actions->addWidget(refresh);
    actions->addStretch(1);
    actions->addWidget(close);
    layout->addLayout(actions);

    auto selectedProfile = [list]() {
        return selectedProfileFrom(list);
    };

    auto updateSortButton = [sortButton]() {
        sortButton->setText(QString("Sort: %1").arg(friendSortLabel(configuredFriendSortMode())));
    };

    auto fillList = [list, status](const QList<RoomMember> &friends) {
        list->clear();
        for (const RoomMember &friendProfile : sortedFriends(friends)) {
            addProfileListItem(list, friendProfile);
        }
        status->setText(friends.isEmpty() ? "No friends returned." : QString("%1 friend%2.").arg(friends.size()).arg(friends.size() == 1 ? "" : "s"));
    };

    auto *sortMenu = new QMenu(sortButton);
    auto addSortAction = [sortMenu, fillList, updateSortButton, callbacks](const QString &mode, const QString &label) {
        QAction *action = sortMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(configuredFriendSortMode() == mode);
        QObject::connect(action, &QAction::triggered, sortMenu, [mode, fillList, updateSortButton, sortMenu, callbacks]() {
            QSettings().setValue("Friends/SortMode", mode);
            for (QAction *sortAction : sortMenu->actions()) {
                sortAction->setChecked(sortAction->data().toString() == mode);
            }
            refreshFriendsUi(callbacks);
            fillList(currentFriends(callbacks));
            updateSortButton();
        });
        action->setData(mode);
    };
    addSortAction("status", "Status");
    addSortAction("name", "Name");
    addSortAction("role", "Role");
    sortButton->setMenu(sortMenu);
    updateSortButton();

    auto reload = [bridge, profileName, list, status, callbacks]() {
        list->clear();
        if (profileName.isEmpty()) {
            status->setText("Log in to load friends.");
            return;
        }

        status->setText("Loading friends...");
        markFriendsLoading(callbacks, true);
        bridge->requestFriends();
        refreshFriendsUi(callbacks);
    };

    QObject::connect(refresh, &QPushButton::clicked, dialog, reload);
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    QObject::connect(list, &QListWidget::itemDoubleClicked, dialog, [callbacks, selectedProfile](QListWidgetItem *) {
        sendPrivateMessage(callbacks, selectedProfile().userId);
    });
    QObject::connect(message, &QPushButton::clicked, dialog, [callbacks, selectedProfile]() {
        sendPrivateMessage(callbacks, selectedProfile().userId);
    });
    QObject::connect(profile, &QPushButton::clicked, dialog, [callbacks, selectedProfile]() {
        const RoomMember selected = selectedProfile();
        openProfile(callbacks, selected.userId, selected.name);
    });
    QObject::connect(remove, &QPushButton::clicked, dialog, [bridge, selectedProfile, status]() {
        const RoomMember selected = selectedProfile();
        if (selected.userId != 0) {
            status->setText(QString("Removing %1...").arg(selected.name));
            bridge->deleteFriend(selected.userId, selected.name);
        }
    });

    QObject::connect(bridge, &MetaClientBridge::friendsChanged, dialog, [fillList](const QList<RoomMember> &friends) {
        fillList(friends);
    });
    QObject::connect(bridge, &MetaClientBridge::errorOccurred, dialog, [status, callbacks](const QString &context, const QString &message) {
        if (context == "Friends") {
            markFriendsLoading(callbacks, false);
            refreshFriendsUi(callbacks);
            status->setText(message);
        }
    });

    dialog->open();
    if (friendsLoaded) {
        fillList(friends);
    } else {
        reload();
    }
}

void showIgnoredUsersDialog(QWidget *parent, MetaClientBridge *bridge)
{
    if (!bridge) {
        return;
    }

    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Ignored Users");
    dialog->resize(480, 340);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *status = new QLabel("Loading ignored users...", dialog);
    status->setObjectName("sectionLabel");
    layout->addWidget(status);

    auto *list = new QListWidget(dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list, 1);

    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton("Add", dialog);
    auto *remove = new QPushButton("Remove", dialog);
    auto *refresh = new QPushButton("Refresh", dialog);
    auto *close = new QPushButton("Close", dialog);
    actions->addWidget(add);
    actions->addWidget(remove);
    actions->addWidget(refresh);
    actions->addStretch(1);
    actions->addWidget(close);
    layout->addLayout(actions);

    auto selectedProfile = [list]() {
        return selectedProfileFrom(list);
    };

    auto reload = [bridge, list, status]() {
        list->clear();
        status->setText("Loading ignored users...");
        bridge->requestIgnores();
    };

    QObject::connect(refresh, &QPushButton::clicked, dialog, reload);
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    QObject::connect(add, &QPushButton::clicked, dialog, [bridge, dialog, status]() {
        bool accepted = false;
        const QString term = QInputDialog::getText(dialog, "Add Ignored User", "User name:", QLineEdit::Normal, {}, &accepted).trimmed();
        if (!accepted || term.isEmpty()) {
            return;
        }

        dialog->setProperty("ignoreSearchTerm", term);
        status->setText(QString("Searching for %1...").arg(term));
        bridge->findProfiles(term);
    });
    QObject::connect(remove, &QPushButton::clicked, dialog, [bridge, selectedProfile]() {
        const RoomMember selected = selectedProfile();
        if (selected.userId != 0) {
            bridge->deleteIgnore(selected.userId, selected.name);
        }
    });

    QObject::connect(bridge, &MetaClientBridge::ignoresChanged, dialog, [list, status](const QList<RoomMember> &ignores) {
        list->clear();
        for (const RoomMember &ignored : ignores) {
            addProfileListItem(list, ignored);
        }
        status->setText(ignores.isEmpty() ? "No ignored users." : QString("%1 ignored user%2.").arg(ignores.size()).arg(ignores.size() == 1 ? "" : "s"));
    });
    QObject::connect(bridge, &MetaClientBridge::profilesFound, dialog, [bridge, dialog, status](const QString &term, const QList<RoomMember> &profiles) {
        const QString pendingTerm = dialog->property("ignoreSearchTerm").toString();
        if (pendingTerm.isEmpty() || term.compare(pendingTerm, Qt::CaseInsensitive) != 0) {
            return;
        }
        dialog->setProperty("ignoreSearchTerm", {});

        if (profiles.isEmpty()) {
            status->setText(QString("No users found for %1.").arg(term));
            return;
        }

        int selectedIndex = -1;
        for (int i = 0; i < profiles.size(); ++i) {
            if (profiles.at(i).name.compare(term, Qt::CaseInsensitive) == 0) {
                selectedIndex = i;
                break;
            }
        }

        if (selectedIndex < 0 && profiles.size() == 1) {
            selectedIndex = 0;
        } else if (selectedIndex < 0) {
            QStringList choices;
            for (const RoomMember &profile : profiles) {
                choices << QString("%1  (#%2)").arg(profile.name).arg(profile.userId);
            }
            bool accepted = false;
            const QString choice = QInputDialog::getItem(dialog, "Add Ignored User", "Select a user:", choices, 0, false, &accepted);
            if (!accepted) {
                status->setText("Add cancelled.");
                return;
            }
            selectedIndex = choices.indexOf(choice);
        }

        if (selectedIndex >= 0 && selectedIndex < profiles.size()) {
            const RoomMember &selected = profiles.at(selectedIndex);
            status->setText(QString("Adding %1...").arg(selected.name));
            bridge->addIgnore(selected.userId, selected.name);
        }
    });
    QObject::connect(bridge, &MetaClientBridge::errorOccurred, dialog, [dialog, status](const QString &context, const QString &message) {
        if (context == "Ignored Users") {
            status->setText(message);
        } else if (context == "Find User" && !dialog->property("ignoreSearchTerm").toString().isEmpty()) {
            dialog->setProperty("ignoreSearchTerm", {});
            status->setText(message);
        }
    });

    dialog->open();
    reload();
}