#include "core/AppPaths.h"
#include "ui/EmoteStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {

constexpr auto kInitializedKey = "Emotes/Initialized";
constexpr auto kItemsArrayKey = "Emotes/Items";

EmoteEntry makeEmote(const QString &name, const QString &text)
{
    return {name, text};
}

QList<EmoteEntry> loadSettingsEmotes(bool *loaded)
{
    QSettings settings;
    if (!settings.value(kInitializedKey, false).toBool()) {
        if (loaded) {
            *loaded = false;
        }
        return {};
    }
    if (loaded) {
        *loaded = true;
    }

    QList<EmoteEntry> emotes;
    const int count = settings.beginReadArray(kItemsArrayKey);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        EmoteEntry entry;
        entry.name = settings.value("Name").toString().trimmed();
        entry.text = settings.value("Text").toString().trimmed();
        if (!entry.name.isEmpty() && !entry.text.isEmpty()) {
            emotes.append(entry);
        }
    }
    settings.endArray();
    return emotes;
}

QList<EmoteEntry> loadFileEmotes(bool *loaded)
{
    QFile file(EmoteStore::storageFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (loaded) {
            *loaded = false;
        }
        return {};
    }
    if (loaded) {
        *loaded = true;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonArray items = document.object().value("emotes").toArray();
    QList<EmoteEntry> emotes;
    for (const QJsonValue &value : items) {
        const QJsonObject object = value.toObject();
        EmoteEntry entry;
        entry.name = object.value("name").toString().trimmed();
        entry.text = object.value("text").toString().trimmed();
        if (!entry.name.isEmpty() && !entry.text.isEmpty()) {
            emotes.append(entry);
        }
    }
    return emotes;
}

void saveFileEmotes(const QList<EmoteEntry> &emotes)
{
    QDir().mkpath(EmoteStore::storageFolder());

    QJsonArray items;
    for (const EmoteEntry &emote : emotes) {
        QJsonObject item;
        item.insert("name", emote.name.trimmed());
        item.insert("text", emote.text.trimmed());
        items.append(item);
    }

    QJsonObject root;
    root.insert("emotes", items);
    QFile file(EmoteStore::storageFilePath());
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

} // namespace

namespace EmoteStore {

QString storageFolder()
{
    return QDir(AppPaths::localDataRoot("GameCQ", QDir::homePath() + "/GameCQ")).filePath("Emotes");
}

QString storageFilePath()
{
    return QDir(storageFolder()).filePath("emotes.json");
}

QList<EmoteEntry> defaultEmotes()
{
    return {
        makeEmote("Clap", "/me begins applauding $d..."),
        makeEmote("Slap", "/me slaps $d in the head..."),
        makeEmote("Smile", "/me smiles at $d..."),
        makeEmote("Wink", "/me winks suspiciously at $d..."),
        makeEmote("Laugh", "/me laughs at $d..."),
        makeEmote("Giggle", "/me giggles at $d..."),
        makeEmote("Nod", "/me nods at $d..."),
        makeEmote("Wave", "/me waves to $d..."),
        makeEmote("Hug", "/me gives $d a great big hug..."),
        makeEmote("Kiss", "/me kisses $d..."),
    };
}

QList<EmoteEntry> loadEmotes()
{
    bool loaded = false;
    QList<EmoteEntry> emotes = loadFileEmotes(&loaded);
    if (loaded) {
        return emotes;
    }

    emotes = loadSettingsEmotes(&loaded);
    if (loaded) {
        saveFileEmotes(emotes);
        QSettings().remove("Emotes");
        return emotes;
    }

    return defaultEmotes();
}

void saveEmotes(const QList<EmoteEntry> &emotes)
{
    saveFileEmotes(emotes);
}

void resetEmotes()
{
    saveEmotes(defaultEmotes());
}

QString renderEmoteText(const EmoteEntry &emote, const QString &senderName, const QString &targetName)
{
    QString text = emote.text.trimmed();
    text.replace("$s", senderName);
    text.replace("$d", targetName);
    text.replace("$g", "GothThug");
    text.replace("$t", "Two Weeks&trade;");
    return text;
}

} // namespace EmoteStore
