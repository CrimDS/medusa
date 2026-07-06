#pragma once

#include "net/MetaClientTypes.h"

#include <QList>
#include <QString>
#include <functional>

class MetaClientBridge;
class QWidget;

struct UserDialogsCallbacks {
    std::function<void(const QString &command)> setChatCommand;
    std::function<void(quint32 userId, const QString &name)> openProfile;
    std::function<void(bool loading)> setFriendsLoading;
    std::function<void()> refreshFriendsUi;
    std::function<QList<RoomMember>()> currentFriends;
};

void showFindUserDialog(QWidget *parent, MetaClientBridge *bridge, const UserDialogsCallbacks &callbacks);
void showFriendsDialog(QWidget *parent,
                       MetaClientBridge *bridge,
                       const QString &profileName,
                       bool friendsLoaded,
                       const QList<RoomMember> &friends,
                       const UserDialogsCallbacks &callbacks);
void showIgnoredUsersDialog(QWidget *parent, MetaClientBridge *bridge);