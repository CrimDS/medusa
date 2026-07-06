#pragma once

#include "net/MetaClientTypes.h"

#include <Qt>
#include <QString>
#include <QtGlobal>

class QListWidget;
class QListWidgetItem;

constexpr quint32 kRoomModeratedFlag = 0x2;
constexpr quint32 kRoomPasswordFlag = 0x4;
constexpr quint32 kRoomPrivateFlag = 0x8;

constexpr int kSideItemKindRole = Qt::UserRole + 70;
constexpr int kSideRoomIdRole = Qt::UserRole + 71;
constexpr int kSideRoomFlagsRole = Qt::UserRole + 72;
constexpr int kSideRoomNameRole = Qt::UserRole + 73;

QString roomMenuLabel(const ChatRoomInfo &room);
QListWidgetItem *addSideDrawerStatusItem(QListWidget *list, const QString &text);
QListWidgetItem *addSideDrawerRoomItem(QListWidget *list, const ChatRoomInfo &room);
QListWidgetItem *addSideDrawerProfileItem(QListWidget *list, const RoomMember &member);