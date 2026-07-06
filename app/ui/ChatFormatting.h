#pragma once

#include <QtGlobal>
#include <QString>

QString chatTime(quint32 seconds);
QString escapedHtmlAttribute(QString value);
// Converts the user's selected color into the legacy wire format expected by
// old GCQL clients, while the renderer separately turns incoming markup into
// safe HTML for the new client.
QString legacyOutgoingChatColor(const QString &colorText);
QString normalizedOutgoingChatText(QString text);
QString legacyChatMarkupToHtml(const QString &text);
QString richTextToPlainText(const QString &html);
QString legacyChatMarkupToPlainText(const QString &text);
QString logTime(quint32 seconds = 0);
QString chatNameClass(quint32 authorId);
