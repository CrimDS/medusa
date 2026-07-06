#include "ui/MainWindow.h"

#include "ui/ChatFormatting.h"
#include "ui/ChatHtmlRenderer.h"
#include "ui/ChatLogWriter.h"
#include "ui/ChatMediaCache.h"
#include "ui/ChatUiSupport.h"
#include "ui/DiscordChatMedia.h"
#include "ui/ThemeManager.h"

#include <QImage>
#include <QSettings>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>

#ifdef GAMECQ_HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineView>
#endif

using namespace gamecq::chat_ui;

void MainWindow::writeChatLogLine(const QString &line, quint32 seconds)
{
    m_chatLogWriter->writeLine(line, seconds);
}

void MainWindow::refreshChatLogTheme()
{
    if (!m_chatLog && !m_chatWebLog) {
        return;
    }

#ifdef GAMECQ_HAS_WEBENGINE
    if (auto *view = qobject_cast<QWebEngineView *>(m_chatWebLog)) {
        // WebEngine chat uses a single generated document. Replacing the whole
        // document is slower than incremental append, but it is the reliable
        // path when the theme changes or old lines were trimmed.
        m_chatWebReady = false;
        view->setHtml(renderChatDocumentHtml(m_chatHtmlLines), QUrl("https://gamecq.invalid/chat/"));
        return;
    }
#endif

    auto *scroll = m_chatLog->verticalScrollBar();
    const bool stickToBottom = scroll && scroll->value() >= scroll->maximum() - 4;
    const int previousScroll = scroll ? scroll->value() : 0;
    const QStringList lines = m_chatHtmlLines;

    m_chatLog->clear();
    m_chatLog->document()->setDefaultStyleSheet(ThemeManager::chatDocumentStyleSheet());
    m_chatMediaCache->addResources(m_chatLog->document());
    for (const QString &line : lines) {
        m_chatLog->append(line);
    }

    if (scroll) {
        scroll->setValue(stickToBottom ? scroll->maximum() : qMin(previousScroll, scroll->maximum()));
    }
}

void MainWindow::loadRecentChatHistory()
{
    if (!QSettings().value("Chat/LoadHistoryOnLogin", false).toBool()) {
        return;
    }

    const QStringList lines = m_chatLogWriter->recentLogLines(kMaxChatDisplayLines - 16);
    if (lines.isEmpty()) {
        return;
    }

    const RenderedChatLine divider = renderChatHistoryDivider(QString("Previous local chat log (%1 lines)").arg(lines.size()));
    if (divider.valid) {
        appendChatHtmlLine(divider.html);
    }

    for (const QString &line : lines) {
        const RenderedChatLine rendered = renderHistoricalChatLogLine(line);
        if (rendered.valid) {
            appendChatHtmlLine(rendered.html);
        }
    }
}

QString MainWindow::discordMediaPresentation(QString *text)
{
    if (!text) {
        return {};
    }

    const DiscordRelaySettings relaySettings = discordRelaySettings();

    const QList<DiscordChatMedia> media = discordChatMedia(*text);
    if (media.isEmpty()) {
        return {};
    }

    QString presentation;
    for (const DiscordChatMedia &item : media) {
        const QString label = friendlyDiscordMediaLabel(item);
        const QString href = item.url.toString(QUrl::FullyEncoded);
        const bool gifMedia = isDiscordGifMedia(item);
        if ((gifMedia && !relaySettings.gifs) || (!gifMedia && !relaySettings.images)) {
            continue;
        }

        const bool trustedImage = isDiscordVisualMedia(item);
        const bool trustedEmbed = isTrustedDiscordMediaHost(item.url.host()) && item.embeddedPage;

        // Only render media from hosts and URL forms that the Discord relay
        // parser recognizes. Everything else remains normal linked chat text.
        if (!trustedImage && !trustedEmbed) {
            continue;
        }

        if (trustedEmbed) {
            presentation += QString("<div class='discord-media'><iframe src=\"%1\" title=\"GIF\" loading=\"lazy\" sandbox=\"allow-scripts allow-same-origin\"></iframe></div>")
                                .arg(escapedHtmlAttribute(href));
        } else {
            presentation += QString("<div class='discord-media'><img src=\"%1\" alt=\"%2\" loading=\"lazy\"/></div>")
                                .arg(escapedHtmlAttribute(href), escapedHtmlAttribute(label));
            if (m_chatLog) {
                requestChatMedia(item.url, item.url);
            }
        }
    }

    for (auto it = media.crbegin(); it != media.crend(); ++it) {
        // Remove visual URLs from the text after creating previews so Discord
        // image/sticker messages read like native chat entries instead of a
        // URL followed by the same media.
        if (isDiscordVisualMedia(*it)) {
            text->replace(it->start, it->length, QString());
        }
    }
    return presentation;
}

void MainWindow::requestChatMedia(const QUrl &source, const QUrl &resource)
{
    if (!m_chatLog) {
        return;
    }

    if (m_chatMediaCache->request(source, resource)) {
        QImage placeholder(1, 1, QImage::Format_ARGB32_Premultiplied);
        placeholder.fill(Qt::transparent);
        m_chatLog->document()->addResource(QTextDocument::ImageResource, resource, placeholder);
    }
}

void MainWindow::appendChatHtmlLine(const QString &line)
{
    if (!m_chatLog && !m_chatWebLog) {
        return;
    }

    m_chatHtmlLines.append(line);
    bool trimmed = false;
    while (m_chatHtmlLines.size() > kMaxChatDisplayLines) {
        // Keep long sessions responsive without changing the on-disk chat log.
        m_chatHtmlLines.removeFirst();
        trimmed = true;
    }
    if (m_chatLog) {
        m_chatLog->append(line);
        if (trimmed) {
            refreshChatLogTheme();
        }
        return;
    }
#ifdef GAMECQ_HAS_WEBENGINE
    if (m_chatWebReady) {
        if (trimmed) {
            refreshChatLogTheme();
            return;
        }
        if (auto *view = qobject_cast<QWebEngineView *>(m_chatWebLog)) {
            view->page()->runJavaScript(renderAppendChatLineScript(line));
        }
    }
#endif
}

void MainWindow::appendSystemChatLine(const QString &message)
{
    if (!m_chatLog && !m_chatWebLog) {
        return;
    }

    const RenderedChatLine rendered = renderSystemChatLine(message);
    if (!rendered.valid) {
        return;
    }

    appendChatHtmlLine(rendered.html);
    writeChatLogLine(rendered.logLine, rendered.logSeconds);
}

void MainWindow::appendChatMessage(const ChatMessage &message)
{
    if (!m_chatLog && !m_chatWebLog) {
        return;
    }

    QString text = message.text;
    const DiscordRelayContent discordContent = discordRelayContent(text);
    if (discordContent.isRelay) {
        // Relay settings suppress whole Discord message categories. They do
        // more than hide previews; disabled categories do not appear in chat.
        const DiscordRelaySettings relaySettings = discordRelaySettings();
        bool allowed = false;
        switch (discordContent.category) {
        case DiscordRelayContent::Category::Text:
            allowed = relaySettings.text;
            break;
        case DiscordRelayContent::Category::Image:
            allowed = relaySettings.images;
            break;
        case DiscordRelayContent::Category::Gif:
            allowed = relaySettings.gifs;
            break;
        case DiscordRelayContent::Category::None:
            allowed = false;
            break;
        }
        if (!allowed) {
            return;
        }
        if ((discordContent.category == DiscordRelayContent::Category::Image
             || discordContent.category == DiscordRelayContent::Category::Gif)
            && !relaySettings.text) {
            text = discordRelayMediaOnlyText(text, discordContent.media);
        }
    }

    const QString mediaPresentation = text.startsWith('/') ? QString() : discordMediaPresentation(&text);
    const RenderedChatLine rendered = renderChatMessageLine(message, text, mediaPresentation);
    if (!rendered.valid) {
        return;
    }

    appendChatHtmlLine(rendered.html);
    writeChatLogLine(rendered.logLine, rendered.logSeconds);
}

