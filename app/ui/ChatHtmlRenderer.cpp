#include "ui/ChatHtmlRenderer.h"

#include "ui/ChatFormatting.h"
#include "ui/ThemeManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

RenderedChatLine renderSystemChatLine(const QString &message)
{
    RenderedChatLine rendered;
    rendered.html = QString("<p><span class='time'>%1</span> <span class='sys'>%2</span></p>")
                        .arg(chatTime(0), message);
    rendered.logLine = QString("[SYS] %1").arg(richTextToPlainText(message));
    rendered.valid = true;
    return rendered;
}

RenderedChatLine renderChatMessageLine(const ChatMessage &message, QString text, const QString &mediaPresentation)
{
    RenderedChatLine rendered;
    rendered.logSeconds = message.time;

    if (text.startsWith('/')) {
        text.remove(0, 1);
        rendered.html = QString("<p><span class='time'>%1</span> <span class='sys'>%2</span></p>")
                            .arg(chatTime(message.time), legacyChatMarkupToHtml(text));
        rendered.logLine = QString("[SYS] %1").arg(legacyChatMarkupToPlainText(text));
        rendered.valid = true;
        return rendered;
    }

    const QString privateMarker = message.recipientId != 0 ? "<span class='pm'>PM</span> " : QString();
    const QString logPrivateMarker = message.recipientId != 0 ? "[PM] " : QString();
    rendered.html = QString("<p><span class='time'>%1</span> %2<span class='author %3'>%4</span> %5</p>")
                        .arg(chatTime(message.time),
                             privateMarker,
                             chatNameClass(message.authorId),
                             message.author.toHtmlEscaped(),
                             legacyChatMarkupToHtml(text) + mediaPresentation);
    rendered.logLine = QString("%1%2: %3").arg(logPrivateMarker, message.author, legacyChatMarkupToPlainText(text));
    rendered.valid = true;
    return rendered;
}

RenderedChatLine renderHistoricalChatLogLine(const QString &line)
{
    static const QRegularExpression logLinePattern(R"(^\[(\d{4}-\d{2}-\d{2})\s+(\d{2}:\d{2})(?::\d{2})?\]\s+(.*)$)");

    const auto match = logLinePattern.match(line.trimmed());
    if (!match.hasMatch()) {
        if (line.startsWith("--- Session log ") && line.endsWith(" ---")) {
            return renderChatHistoryDivider(line.mid(4, line.size() - 8).trimmed());
        }
        return {};
    }

    const QString stamp = QString("%1 %2").arg(match.captured(1), match.captured(2));
    QString body = match.captured(3).trimmed();

    RenderedChatLine rendered;
    rendered.valid = true;

    if (body.startsWith("[SYS] ")) {
        body.remove(0, 6);
        rendered.html = QString("<p class='history'><span class='time'>%1</span> <span class='sys'>%2</span></p>")
                            .arg(stamp.toHtmlEscaped(), legacyChatMarkupToHtml(body));
        return rendered;
    }

    QString privateMarker;
    if (body.startsWith("[PM] ")) {
        body.remove(0, 5);
        privateMarker = "<span class='pm'>PM</span> ";
    }

    const int separator = body.indexOf(": ");
    if (separator <= 0) {
        rendered.html = QString("<p class='history'><span class='time'>%1</span> <span class='sys'>%2</span></p>")
                            .arg(stamp.toHtmlEscaped(), legacyChatMarkupToHtml(body));
        return rendered;
    }

    const QString author = body.left(separator);
    const QString text = body.mid(separator + 2);
    rendered.html = QString("<p class='history'><span class='time'>%1</span> %2<span class='author author-primary'>%3</span> %4</p>")
                        .arg(stamp.toHtmlEscaped(),
                             privateMarker,
                             author.toHtmlEscaped(),
                             legacyChatMarkupToHtml(text));
    return rendered;
}

RenderedChatLine renderChatHistoryDivider(const QString &message)
{
    RenderedChatLine rendered;
    rendered.html = QString("<p class='history-divider'><span>%1</span></p>").arg(message.toHtmlEscaped());
    rendered.valid = true;
    return rendered;
}

QString renderChatDocumentHtml(const QStringList &htmlLines)
{
    return QString(R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; img-src https: data:; frame-src https://tenor.com https://giphy.com; style-src 'unsafe-inline'">
<style>
html,body{margin:0;min-height:100%;background:%1;color:%2;overflow-wrap:anywhere}
body{box-sizing:border-box;padding:12px 18px}
#messages{min-height:100%}
%3
</style></head><body><div id="messages">%4</div></body></html>)HTML")
        .arg(ThemeManager::colorName("pageBg"),
             ThemeManager::colorName("text"),
             ThemeManager::chatDocumentStyleSheet(),
             htmlLines.join(QString()));
}

QString renderReplaceChatMessagesScript(const QStringList &htmlLines)
{
    const QJsonArray content{htmlLines.join(QString())};
    const QString json = QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact));
    return QString("document.getElementById('messages').innerHTML=%1[0];window.scrollTo(0,document.body.scrollHeight);")
        .arg(json);
}

QString renderAppendChatLineScript(const QString &htmlLine)
{
    const QJsonArray content{htmlLine};
    const QString json = QString::fromUtf8(QJsonDocument(content).toJson(QJsonDocument::Compact));
    return QString(R"JS((()=>{const nearBottom=window.innerHeight+window.scrollY>=document.body.scrollHeight-24;document.getElementById('messages').insertAdjacentHTML('beforeend',%1[0]);if(nearBottom)window.scrollTo(0,document.body.scrollHeight);})();)JS")
        .arg(json);
}
