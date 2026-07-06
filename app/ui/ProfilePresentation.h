#pragma once

#include "net/MetaClientTypes.h"

#include <QColor>
#include <QIcon>
#include <QList>
#include <QString>
#include <Qt>
#include <QtGlobal>

class QListWidget;
class QListWidgetItem;

constexpr int kMemberUserIdRole = Qt::UserRole;
constexpr int kMemberNameRole = Qt::UserRole + 1;
constexpr int kMemberFlagsRole = Qt::UserRole + 2;
constexpr int kMemberStatusRole = Qt::UserRole + 3;

QString memberSuffix(quint32 flags);
QColor profileAccentColor(quint32 flags);
QIcon makeProfileIcon(const QString &name, quint32 flags);
bool roomMemberLessThan(const RoomMember &left, const RoomMember &right);
QString configuredFriendSortMode();
QString friendSortLabel(const QString &mode);
QList<RoomMember> sortedFriends(QList<RoomMember> friends, const QString &mode = configuredFriendSortMode());
void applyProfileItemVisuals(QListWidgetItem *item, const RoomMember &profile);
QString profileListLabel(const RoomMember &profile);
void addProfileListItem(QListWidget *list, const RoomMember &profile);