#include "ui/ChatFormatting.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

void appendCodePoint(QString &text, uint codePoint)
{
    if (codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
        return;
    }

    if (codePoint <= 0xffff) {
        text += QChar(static_cast<ushort>(codePoint));
        return;
    }

    codePoint -= 0x10000;
    text += QChar(static_cast<ushort>(0xd800 + (codePoint >> 10)));
    text += QChar(static_cast<ushort>(0xdc00 + (codePoint & 0x3ff)));
}

QString decodeLegacyChatEntity(const QString &entity)
{
    const QString key = entity.toLower();
    if (key == "amp") {
        return "&";
    }
    if (key == "lt") {
        return "<";
    }
    if (key == "gt") {
        return ">";
    }
    if (key == "quot") {
        return "\"";
    }
    if (key == "apos") {
        return "'";
    }
    if (key == "nbsp") {
        return " ";
    }
    if (key == "acute") {
        return QString(QChar(0x00b4));
    }
    if (key == "copy") {
        return QString(QChar(0x00a9));
    }
    if (key == "reg") {
        return QString(QChar(0x00ae));
    }
    if (key == "trade") {
        return QString(QChar(0x2122));
    }
    if (key == "hearts") {
        return QString(QChar(0x2665));
    }

    bool ok = false;
    uint codePoint = 0;
    if (key.startsWith("#x")) {
        codePoint = key.mid(2).toUInt(&ok, 16);
    } else if (key.startsWith('#')) {
        codePoint = key.mid(1).toUInt(&ok, 10);
    }

    if (!ok || (codePoint < 0x20 && codePoint != '\t' && codePoint != '\n' && codePoint != '\r')) {
        return {};
    }

    QString decoded;
    appendCodePoint(decoded, codePoint);
    return decoded;
}

QString decodeLegacyChatEntitiesOnce(const QString &text)
{
    static const QRegularExpression entityExpression("&(#x[0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]+);");

    QString decoded;
    qsizetype cursor = 0;
    QRegularExpressionMatchIterator matches = entityExpression.globalMatch(text);
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        decoded += text.mid(cursor, match.capturedStart() - cursor);

        const QString replacement = decodeLegacyChatEntity(match.captured(1));
        decoded += replacement.isEmpty() ? match.captured(0) : replacement;
        cursor = match.capturedEnd();
    }

    decoded += text.mid(cursor);
    return decoded;
}

QString decodeLegacyChatEntities(QString text)
{
    // Some old bot/server text arrives double-escaped. Two passes handle common
    // legacy cases without risking an unbounded decode loop.
    for (int i = 0; i < 2; ++i) {
        const QString decoded = decodeLegacyChatEntitiesOnce(text);
        if (decoded == text) {
            break;
        }
        text = decoded;
    }
    return text;
}

QString escapedHtmlAttribute(QString value)
{
    value = value.toHtmlEscaped();
    value.replace('\'', "&#39;");
    return value;
}

QString safeLegacyChatColor(QString color)
{
    color = color.trimmed();
    if (color.startsWith('#')) {
        color.remove(0, 1);
    }

    static const QRegularExpression colorExpression("^[0-9a-fA-F]{6}$");
    if (!colorExpression.match(color).hasMatch()) {
        return {};
    }

    return QString("#%1").arg(color.toLower());
}


QString safeLegacyChatUrl(QString urlText)
{
    urlText = decodeLegacyChatEntities(urlText).trimmed();
    if (urlText.isEmpty()) {
        return {};
    }

    if (urlText.startsWith("www.", Qt::CaseInsensitive)) {
        urlText.prepend("http://");
    } else if (urlText.startsWith("ftp.", Qt::CaseInsensitive)) {
        urlText.prepend("ftp://");
    }

    QUrl url(urlText);
    if (!url.isValid() || url.scheme().isEmpty()) {
        url = QUrl::fromUserInput(urlText);
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != "http" && scheme != "https" && scheme != "ftp" && scheme != "mailto") {
        return {};
    }

    return url.toString(QUrl::FullyEncoded);
}

QString closingLegacyChatTag(const QString &tag)
{
    if (tag == "b") {
        return "</b>";
    }
    if (tag == "i") {
        return "</i>";
    }
    if (tag == "u") {
        return "</u>";
    }
    if (tag == "a") {
        return "</a>";
    }
    if (tag == "span") {
        return "</span>";
    }
    return {};
}

bool closeLegacyChatTag(QString &html, QStringList &openTags, const QString &tag)
{
    const int index = openTags.lastIndexOf(tag);
    if (index < 0) {
        // Old help text sometimes begins with stray closing tags. Swallow those
        // instead of showing raw markup to the user.
        return false;
    }

    html += closingLegacyChatTag(tag);
    openTags.removeAt(index);
    return true;
}

void appendEscapedChatText(QString &html, const QString &text)
{
    static const QRegularExpression urlExpression("((?:https?://|ftp://|www\\.)[^\\s<>\\[\\]\"']+)");

    qsizetype cursor = 0;
    while (cursor < text.size()) {
        const QRegularExpressionMatch match = urlExpression.match(text, cursor);
        if (!match.hasMatch()) {
            html += text.mid(cursor).toHtmlEscaped();
            return;
        }

        html += text.mid(cursor, match.capturedStart() - cursor).toHtmlEscaped();

        QString urlText = match.captured(1);
        QString trailing;
        while (!urlText.isEmpty() && QString(".,;:!?)]}").contains(urlText.back())) {
            trailing.prepend(urlText.back());
            urlText.chop(1);
        }

        const QString href = safeLegacyChatUrl(urlText);
        if (href.isEmpty()) {
            html += match.captured(1).toHtmlEscaped();
        } else {
            html += QString("<a href=\"%1\">%2</a>").arg(escapedHtmlAttribute(href), urlText.toHtmlEscaped());
        }
        html += trailing.toHtmlEscaped();

        cursor = match.capturedEnd();
    }
}

bool tryAppendLegacySquareTag(const QString &source, qsizetype tagStart, qsizetype tagEnd, QString &html, QStringList &openTags, qsizetype &nextIndex)
{
    const QString tag = source.mid(tagStart + 1, tagEnd - tagStart - 1).trimmed();
    const QString lower = tag.toLower();

    if (lower == "b") {
        html += "<b>";
        openTags += "b";
    } else if (lower == "i") {
        html += "<i>";
        openTags += "i";
    } else if (lower == "u") {
        html += "<u>";
        openTags += "u";
    } else if (lower == "/b") {
        closeLegacyChatTag(html, openTags, "b");
    } else if (lower == "/i") {
        closeLegacyChatTag(html, openTags, "i");
    } else if (lower == "/u") {
        closeLegacyChatTag(html, openTags, "u");
    } else if (lower.startsWith("color=")) {
        const QString color = safeLegacyChatColor(tag.mid(6));
        if (color.isEmpty()) {
            return false;
        }
        html += QString("<span style=\"color:%1\">").arg(color);
        openTags += "span";
    } else if (lower == "/color") {
        closeLegacyChatTag(html, openTags, "span");
    } else if (lower == "url") {
        // [url]...[/url] uses the label as the destination; [url=...]text[/url]
        // is handled by the normal open/close tag path below.
        static const QRegularExpression closeUrlExpression("\\[/url\\]", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch closeMatch = closeUrlExpression.match(source, tagEnd + 1);
        if (!closeMatch.hasMatch()) {
            return false;
        }

        const QString label = source.mid(tagEnd + 1, closeMatch.capturedStart() - tagEnd - 1);
        const QString href = safeLegacyChatUrl(label);
        if (href.isEmpty()) {
            appendEscapedChatText(html, label);
        } else {
            html += QString("<a href=\"%1\">%2</a>").arg(escapedHtmlAttribute(href), label.toHtmlEscaped());
        }
        nextIndex = closeMatch.capturedEnd();
        return true;
    } else if (lower.startsWith("url=")) {
        const QString href = safeLegacyChatUrl(tag.mid(4));
        if (href.isEmpty()) {
            return false;
        }
        html += QString("<a href=\"%1\">").arg(escapedHtmlAttribute(href));
        openTags += "a";
    } else if (lower == "/url") {
        closeLegacyChatTag(html, openTags, "a");
    } else {
        return false;
    }

    nextIndex = tagEnd + 1;
    return true;
}

bool tryAppendLegacyAngleTag(const QString &source, qsizetype tagStart, qsizetype tagEnd, QString &html, QStringList &openTags, qsizetype &nextIndex)
{
    const QString tag = source.mid(tagStart + 1, tagEnd - tagStart - 1).trimmed();
    const QString lower = tag.toLower();

    if (lower == "b") {
        html += "<b>";
        openTags += "b";
    } else if (lower == "/b") {
        closeLegacyChatTag(html, openTags, "b");
    } else if (lower == "i") {
        html += "<i>";
        openTags += "i";
    } else if (lower == "/i") {
        closeLegacyChatTag(html, openTags, "i");
    } else if (lower.startsWith("font color=")) {
        const QString color = safeLegacyChatColor(tag.mid(11));
        if (color.isEmpty()) {
            return false;
        }
        html += QString("<span style=\"color:%1\">").arg(color);
        openTags += "span";
    } else if (lower == "/font") {
        closeLegacyChatTag(html, openTags, "span");
    } else {
        return false;
    }

    nextIndex = tagEnd + 1;
    return true;
}

QString legacyChatMarkupToHtml(const QString &text)
{
    // This parser accepts the subset of old GCQL/HTML-like markup used by the
    // server and bots, then escapes everything else. It is intentionally not a
    // general HTML parser.
    QString source = decodeLegacyChatEntities(text);
    source.replace("\r\n", "\n");
    source.replace('\r', '\n');

    QString html;
    html.reserve(source.size() * 2);
    QStringList openTags;

    qsizetype cursor = 0;
    while (cursor < source.size()) {
        qsizetype nextSpecial = source.size();
        const qsizetype nextSquare = source.indexOf('[', cursor);
        const qsizetype nextAngle = source.indexOf('<', cursor);
        const qsizetype nextNewline = source.indexOf('\n', cursor);
        if (nextSquare >= 0) {
            nextSpecial = qMin(nextSpecial, nextSquare);
        }
        if (nextAngle >= 0) {
            nextSpecial = qMin(nextSpecial, nextAngle);
        }
        if (nextNewline >= 0) {
            nextSpecial = qMin(nextSpecial, nextNewline);
        }

        if (nextSpecial > cursor) {
            appendEscapedChatText(html, source.mid(cursor, nextSpecial - cursor));
            cursor = nextSpecial;
            continue;
        }

        const QChar current = source.at(cursor);
        if (current == '\n') {
            html += "<br/>";
            ++cursor;
            continue;
        }

        if (current == '[') {
            const qsizetype tagEnd = source.indexOf(']', cursor + 1);
            qsizetype nextIndex = cursor;
            if (tagEnd > cursor && tryAppendLegacySquareTag(source, cursor, tagEnd, html, openTags, nextIndex)) {
                cursor = nextIndex;
                continue;
            }
        } else if (current == '<') {
            const qsizetype tagEnd = source.indexOf('>', cursor + 1);
            qsizetype nextIndex = cursor;
            if (tagEnd > cursor && tryAppendLegacyAngleTag(source, cursor, tagEnd, html, openTags, nextIndex)) {
                cursor = nextIndex;
                continue;
            }
        }

        html += QString(current).toHtmlEscaped();
        ++cursor;
    }

    for (qsizetype i = openTags.size() - 1; i >= 0; --i) {
        // Balance tolerated legacy markup so one malformed line cannot leak
        // formatting into every line after it.
        html += closingLegacyChatTag(openTags.at(i));
    }

    return html;
}

