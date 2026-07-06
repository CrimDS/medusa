#pragma once

#include <QList>
#include <QString>

struct EmoteEntry {
    QString name;
    QString text;
};

namespace EmoteStore {

QString storageFolder();
QString storageFilePath();
QList<EmoteEntry> defaultEmotes();
QList<EmoteEntry> loadEmotes();
void saveEmotes(const QList<EmoteEntry> &emotes);
void resetEmotes();
QString renderEmoteText(const EmoteEntry &emote, const QString &senderName, const QString &targetName);

} // namespace EmoteStore
