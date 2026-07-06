#pragma once

#include <QList>
#include <QString>
#include <QUrl>

struct DiscordChatMedia {
    QString kind;
    QString label;
    QUrl url;
    qsizetype start = 0;
    qsizetype length = 0;
    bool hadSeparator = false;
    bool compactLink = false;
    bool embeddedPage = false;
};

struct DiscordRelayContent {
    enum class Category {
        None,
        Text,
        Image,
        Gif
    };

    bool isRelay = false;
    bool hasText = false;
    bool hasImages = false;
    bool hasGifs = false;
    Category category = Category::None;
    QList<DiscordChatMedia> media;
};

// Discord relay filtering is category based: options can suppress whole text,
// image, or gif relay messages before the chat renderer creates preview HTML.
bool isTrustedDiscordMediaHost(const QString &host);
QList<DiscordChatMedia> discordChatMedia(const QString &text);
QString friendlyDiscordMediaLabel(const DiscordChatMedia &media);
bool isDiscordGifMedia(const DiscordChatMedia &media);
bool isDiscordVisualMedia(const DiscordChatMedia &media);
QString discordRelayBody(QString text, const QList<DiscordChatMedia> &media);
QString discordRelayMediaOnlyText(const QString &text, const QList<DiscordChatMedia> &media);
DiscordRelayContent discordRelayContent(const QString &text);
