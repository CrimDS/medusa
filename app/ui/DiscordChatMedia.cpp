#include "ui/DiscordChatMedia.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace {

bool hasImageSuffix(const QString &text)
{
    const QString suffix = QFileInfo(QUrl(text).path()).suffix().toLower();
    return suffix == "png" || suffix == "jpg" || suffix == "jpeg"
        || suffix == "gif" || suffix == "webp" || suffix == "bmp";
}

} // namespace

bool isTrustedDiscordMediaHost(const QString &host)
{
    const QString lower = host.toLower();
    return lower == "cdn.discordapp.com"
        || lower == "media.discordapp.net"
        || lower == "tenor.com"
        || lower == "giphy.com"
        || lower.endsWith(".discordapp.com")
        || lower.endsWith(".discordapp.net")
        || lower.endsWith(".discordcdn.com")
        || lower.endsWith(".tenor.com")
        || lower.endsWith(".giphy.com");
}

QList<DiscordChatMedia> discordChatMedia(const QString &text)
{
    if (!text.startsWith("[Discord] ", Qt::CaseInsensitive)) {
        return {};
    }

    static const QRegularExpression legacyExpression(
        "(\\s*\\|\\s*)?\\b(attachment|sticker)\\s+(.+?):\\s+(https://[^\\s|]+)",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression compactExpression(
        "\\[url=(https://[^\\]]+)\\]([^\\[]+)\\[/url\\]",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression gifPageExpression(
        "https://(?:www\\.)?(?:giphy\\.com/gifs/[^\\s|<\\[]+|tenor\\.com/view/[^\\s|<\\[]+)",
        QRegularExpression::CaseInsensitiveOption);

    QList<DiscordChatMedia> media;
    QRegularExpressionMatchIterator compactMatches = compactExpression.globalMatch(text);
    while (compactMatches.hasNext() && media.size() < 4) {
        const QRegularExpressionMatch match = compactMatches.next();
        QUrl url(match.captured(1));
        if (!url.isValid() || url.scheme().compare("https", Qt::CaseInsensitive) != 0) {
            continue;
        }

        DiscordChatMedia item;
        item.kind = "attachment";
        item.label = match.captured(2).trimmed();
        item.url = url;
        item.start = match.capturedStart(0);
        item.length = match.capturedLength(0);
        const QString beforeLink = text.left(item.start);
        const QRegularExpressionMatch prefix = QRegularExpression(
            "(?:\\s*\\|\\s*)?\\b(?:shared|sticker)\\s+$",
            QRegularExpression::CaseInsensitiveOption)
                                                   .match(beforeLink);
        if (prefix.hasMatch()) {
            item.start = prefix.capturedStart(0);
            item.length = match.capturedEnd(0) - item.start;
        }
        item.compactLink = true;
        media.append(item);
    }

    QRegularExpressionMatchIterator matches = legacyExpression.globalMatch(text);
    while (matches.hasNext() && media.size() < 4) {
        const QRegularExpressionMatch match = matches.next();
        QUrl url(match.captured(4));
        if (!url.isValid() || url.scheme().compare("https", Qt::CaseInsensitive) != 0) {
            continue;
        }

        DiscordChatMedia item;
        item.kind = match.captured(2).toLower();
        item.label = match.captured(3).trimmed();
        item.url = url;
        item.start = match.capturedStart(0);
        item.length = match.capturedLength(0);
        item.hadSeparator = !match.captured(1).isEmpty();
        media.append(item);
    }

    QRegularExpressionMatchIterator gifMatches = gifPageExpression.globalMatch(text);
    while (gifMatches.hasNext() && media.size() < 4) {
        const QRegularExpressionMatch match = gifMatches.next();
        const qsizetype start = match.capturedStart(0);
        const qsizetype length = match.capturedLength(0);
        const bool overlapsExisting = std::any_of(media.cbegin(), media.cend(), [start, length](const DiscordChatMedia &item) {
            return start < item.start + item.length && item.start < start + length;
        });
        if (overlapsExisting) {
            continue;
        }

        QUrl sharedUrl(match.captured(0));
        QString slug = sharedUrl.path().section('/', -1);
        QRegularExpressionMatch idMatch;
        DiscordChatMedia item;
        if (sharedUrl.host().contains("giphy.com", Qt::CaseInsensitive)) {
            idMatch = QRegularExpression("(?:^|-)([A-Za-z0-9]+)$").match(slug);
            if (!idMatch.hasMatch()) {
                continue;
            }
            item.url = QUrl(QString("https://media.giphy.com/media/%1/giphy.gif").arg(idMatch.captured(1)));
        } else {
            idMatch = QRegularExpression("(?:^|-)([0-9]+)$").match(slug);
            if (!idMatch.hasMatch()) {
                continue;
            }
            item.url = QUrl(QString("https://tenor.com/embed/%1").arg(idMatch.captured(1)));
            item.embeddedPage = true;
        }
        item.kind = "gif";
        item.label = "GIF";
        item.start = start;
        item.length = length;
        media.append(item);
    }
    std::sort(media.begin(), media.end(), [](const DiscordChatMedia &left, const DiscordChatMedia &right) {
        return left.start < right.start;
    });
    return media;
}

QString friendlyDiscordMediaLabel(const DiscordChatMedia &media)
{
    QString label = media.label;
    if (label.startsWith("emoji-", Qt::CaseInsensitive)) {
        label.remove(0, 6);
        const int suffix = label.lastIndexOf('.');
        if (suffix > 0) {
            label.truncate(suffix);
        }
        return ':' + label + ':';
    }
    if (label == "embed-preview") {
        return "Media preview";
    }
    return label;
}

bool isDiscordGifMedia(const DiscordChatMedia &media)
{
    return media.embeddedPage
        || media.kind == "gif"
        || media.label.endsWith(".gif", Qt::CaseInsensitive)
        || media.url.path().endsWith(".gif", Qt::CaseInsensitive);
}

bool isDiscordVisualMedia(const DiscordChatMedia &media)
{
    if (!isTrustedDiscordMediaHost(media.url.host())) {
        return false;
    }
    return media.embeddedPage
        || media.kind == "sticker"
        || media.kind == "gif"
        || media.label == "embed-preview"
        || media.label == "media preview"
        || media.label.startsWith("emoji-", Qt::CaseInsensitive)
        || hasImageSuffix(media.label)
        || hasImageSuffix(media.url.path());
}

QString discordRelayBody(QString text, const QList<DiscordChatMedia> &media)
{
    for (auto it = media.crbegin(); it != media.crend(); ++it) {
        if (isDiscordVisualMedia(*it)) {
            text.replace(it->start, it->length, QString());
        }
    }

    text.remove(QRegularExpression("^\\[Discord\\]\\s*[^:]+:\\s*", QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression("\\b(?:shared|sticker)\\s+\\S+\\s*$", QRegularExpression::CaseInsensitiveOption));
    text.replace(QRegularExpression("\\s+"), " ");
    return text.trimmed();
}

QString discordRelayMediaOnlyText(const QString &text, const QList<DiscordChatMedia> &media)
{
    const QRegularExpressionMatch prefix = QRegularExpression("^\\[Discord\\]\\s*[^:]+:", QRegularExpression::CaseInsensitiveOption).match(text);
    QString result = prefix.hasMatch() ? prefix.captured(0) : QString("[Discord]");
    for (const DiscordChatMedia &item : media) {
        if (isDiscordVisualMedia(item)) {
            result += ' ' + text.mid(item.start, item.length).trimmed();
        }
    }
    return result;
}

DiscordRelayContent discordRelayContent(const QString &text)
{
    DiscordRelayContent result;
    result.isRelay = text.startsWith("[Discord] ", Qt::CaseInsensitive);
    if (!result.isRelay) {
        return result;
    }

    result.media = discordChatMedia(text);
    for (const DiscordChatMedia &item : result.media) {
        if (!isDiscordVisualMedia(item)) {
            continue;
        }
        if (isDiscordGifMedia(item)) {
            result.hasGifs = true;
        } else {
            result.hasImages = true;
        }
    }
    result.hasText = !discordRelayBody(text, result.media).isEmpty();
    if (result.hasGifs) {
        result.category = DiscordRelayContent::Category::Gif;
    } else if (result.hasImages) {
        result.category = DiscordRelayContent::Category::Image;
    } else if (result.hasText) {
        result.category = DiscordRelayContent::Category::Text;
    }
    return result;
}