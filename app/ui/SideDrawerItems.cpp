#include "ui/SideDrawerItems.h"

#include "ui/LegacyIcons.h"
#include "ui/ProfilePresentation.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QStringList>

namespace {

QString roomSuffix(const ChatRoomInfo &room)
{
    QStringList suffixes;
    if ((room.flags & kRoomModeratedFlag) != 0) {
        suffixes << "MODERATED";
    }
    if ((room.flags & kRoomPasswordFlag) != 0) {
        suffixes << "PRIVATE";
    }
    if ((room.flags & kRoomPrivateFlag) != 0) {
        suffixes << "HIDDEN";
    }

    return suffixes.join(" ");
}

} // namespace

QString roomMenuLabel(const ChatRoomInfo &room)
{
    QString label = QString("%1    %2").arg(room.name).arg(room.members);
    const QString suffix = roomSuffix(room);
    if (!suffix.isEmpty()) {
        label += QString("    [%1]").arg(suffix);
    }

    return label;
}

QListWidgetItem *addSideDrawerStatusItem(QListWidget *list, const QString &text)
{
    auto *item = new QListWidgetItem(text, list);
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    return item;
}

QListWidgetItem *addSideDrawerRoomItem(QListWidget *list, const ChatRoomInfo &room)
{
    auto *item = new QListWidgetItem(roomMenuLabel(room), list);
    item->setIcon(legacyIcon("room"));
    item->setData(kSideItemKindRole, "room");
    item->setData(kSideRoomIdRole, room.roomId);
    item->setData(kSideRoomFlagsRole, room.flags);
    item->setData(kSideRoomNameRole, room.name);
    return item;
}

QListWidgetItem *addSideDrawerProfileItem(QListWidget *list, const RoomMember &member)
{
    addProfileListItem(list, member);
    QListWidgetItem *item = list ? list->item(list->count() - 1) : nullptr;
    if (item) {
        item->setData(kSideItemKindRole, "profile");
    }
    return item;
}