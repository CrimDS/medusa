#include "ui/ProfilePresentation.h"

#include "net/ProfileFlags.h"

#include "ui/LegacyIcons.h"
#include "ui/ThemeManager.h"

#include <QListWidgetItem>
#include <QPainter>
#include <QPixmap>
#include <QRadialGradient>
#include <QSettings>
#include <QStringList>

#include <algorithm>

QString memberSuffix(quint32 flags)
{
    QStringList suffixes;
    if ((flags & kAdministratorFlag) != 0) {
        suffixes << "ADMIN";
    } else if ((flags & kModeratorFlag) != 0) {
        suffixes << "MOD";
    }
    if ((flags & kDeveloperFlag) != 0) {
        suffixes << "DEV";
    }
    if ((flags & kAwayFlag) != 0) {
        suffixes << "AWAY";
    }
    if ((flags & kMutedFlag) != 0) {
        suffixes << "MUTED";
    }
    if ((flags & kHiddenFlag) != 0) {
        suffixes << "HIDDEN";
    }

    return suffixes.join(" ");
}

QColor profileAccentColor(quint32 flags)
{
    if ((flags & (kAdministratorFlag | kModeratorFlag)) != 0) {
        return ThemeManager::color("primary");
    }
    if ((flags & kDeveloperFlag) != 0) {
        return ThemeManager::color("purple");
    }
    if ((flags & kAwayFlag) != 0) {
        return ThemeManager::color("warning");
    }
    return ThemeManager::color("success");
}


bool usesCreatorIcon(const QString &name)
{
    return name.toLower().contains("crim");
}

QIcon composeMutedProfileIcon(const QIcon &baseIcon)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPixmap basePixmap = baseIcon.pixmap(16, 16);
    if (!basePixmap.isNull()) {
        painter.drawPixmap(0, 0, basePixmap);
    }

    const QPixmap mutedOverlay = legacyIcon("ico00005").pixmap(16, 16);
    if (!mutedOverlay.isNull()) {
        painter.drawPixmap(0, 0, mutedOverlay);
    }

    return QIcon(pixmap);
}

QIcon makeProfileBaseIcon(quint32 flags)
{
    const QIcon legacy = (flags & kHiddenFlag) != 0 ? legacyIcon("ico00004")
                       : (flags & kAwayFlag) != 0 ? legacyIcon("ico00001")
                       : (flags & kAdministratorFlag) != 0 ? legacyIcon("avatar_a")
                       : (flags & kModeratorFlag) != 0 ? legacyIcon("avatar_m")
                       : (flags & kDeveloperFlag) != 0 ? legacyIcon("avatar_d")
                       : (flags & kSubscribedFlag) != 0 ? legacyIcon("avatar_sub")
                                                        : legacyIcon("Avatar");
    if (!legacy.isNull()) {
        return legacy;
    }

    const QColor accent = profileAccentColor(flags);
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRadialGradient glow(QPointF(9, 8), 8);
    QColor glowColor = accent;
    glowColor.setAlpha(90);
    QColor clearColor = accent;
    clearColor.setAlpha(0);
    glow.setColorAt(0.0, glowColor);
    glow.setColorAt(1.0, clearColor);
    painter.setPen(Qt::NoPen);
    painter.setBrush(glow);
    painter.drawEllipse(QRectF(1, 0, 16, 16));

    QColor body = accent;
    body.setAlpha(225);
    painter.setBrush(body);
    painter.drawEllipse(QRectF(6.0, 3.0, 6.0, 6.0));
    painter.drawRoundedRect(QRectF(4.0, 10.0, 10.0, 5.5), 2.5, 2.5);

    return QIcon(pixmap);
}

QIcon makeProfileIcon(const QString &name, quint32 flags)
{
    const quint32 baseFlags = flags & ~kMutedFlag;
    if ((baseFlags & (kHiddenFlag | kAwayFlag)) == 0 && usesCreatorIcon(name)) {
        const QIcon creatorIcon = legacyIcon("bigfoot");
        if (!creatorIcon.isNull()) {
            return (flags & kMutedFlag) != 0 ? composeMutedProfileIcon(creatorIcon) : creatorIcon;
        }
    }

    const QIcon baseIcon = makeProfileBaseIcon(baseFlags);
    return (flags & kMutedFlag) != 0 ? composeMutedProfileIcon(baseIcon) : baseIcon;
}

int roomMemberSortRank(quint32 flags)
{
    if ((flags & kAwayFlag) != 0) {
        return 5;
    }
    if ((flags & kAdministratorFlag) != 0) {
        return 0;
    }
    if ((flags & kDeveloperFlag) != 0) {
        return 1;
    }
    if ((flags & kModeratorFlag) != 0) {
        return 2;
    }
    if ((flags & kSubscribedFlag) != 0) {
        return 3;
    }
    return 4;
}

bool roomMemberLessThan(const RoomMember &left, const RoomMember &right)
{
    const int leftRank = roomMemberSortRank(left.flags);
    const int rightRank = roomMemberSortRank(right.flags);
    if (leftRank != rightRank) {
        return leftRank < rightRank;
    }

    const int nameCompare = QString::localeAwareCompare(left.name, right.name);
    if (nameCompare != 0) {
        return nameCompare < 0;
    }
    return left.userId < right.userId;
}

QString configuredFriendSortMode()
{
    const QString mode = QSettings().value("Friends/SortMode", "status").toString().trimmed().toLower();
    if (mode == "name" || mode == "role") {
        return mode;
    }
    return "status";
}

QString friendSortLabel(const QString &mode)
{
    if (mode == "name") {
        return "Name";
    }
    if (mode == "role") {
        return "Role";
    }
    return "Status";
}

QList<RoomMember> sortedFriends(QList<RoomMember> friends, const QString &mode)
{
    std::stable_sort(friends.begin(), friends.end(), [mode](const RoomMember &left, const RoomMember &right) {
        if (mode == "role") {
            return roomMemberLessThan(left, right);
        }

        if (mode == "status") {
            const bool leftAway = (left.flags & kAwayFlag) != 0;
            const bool rightAway = (right.flags & kAwayFlag) != 0;
            if (leftAway != rightAway) {
                return !leftAway;
            }
        }

        const int nameCompare = QString::localeAwareCompare(left.name, right.name);
        if (nameCompare != 0) {
            return nameCompare < 0;
        }
        return left.userId < right.userId;
    });
    return friends;
}

void applyProfileItemVisuals(QListWidgetItem *item, const RoomMember &profile)
{
    item->setIcon(makeProfileIcon(profile.name, profile.flags));
    if ((profile.flags & (kAdministratorFlag | kModeratorFlag | kDeveloperFlag)) != 0) {
        item->setForeground(profileAccentColor(profile.flags));
    }
}

QString profileListLabel(const RoomMember &profile)
{
    return profile.name;
}


void addProfileListItem(QListWidget *list, const RoomMember &profile)
{
    auto *item = new QListWidgetItem(profileListLabel(profile), list);
    item->setData(kMemberUserIdRole, profile.userId);
    item->setData(kMemberNameRole, profile.name);
    item->setData(kMemberFlagsRole, profile.flags);
    item->setData(kMemberStatusRole, profile.status);
    if (!profile.status.isEmpty()) {
        item->setToolTip(profile.status);
    }
    applyProfileItemVisuals(item, profile);
}