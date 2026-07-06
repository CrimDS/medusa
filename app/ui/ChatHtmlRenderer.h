#pragma once

#include "net/MetaClientTypes.h"

#include <QString>
#include <QStringList>
#include <QtGlobal>

struct RenderedChatLine {
    QString html;
    QString logLine;
    quint32 logSeconds = 0;
    bool valid = false;
};

RenderedChatLine renderSystemChatLine(const QString &message);
RenderedChatLine renderChatMessageLine(const ChatMessage &message, QString text, const QString &mediaPresentation = {});
RenderedChatLine renderHistoricalChatLogLine(const QString &line);
RenderedChatLine renderChatHistoryDivider(const QString &message);
QString renderChatDocumentHtml(const QStringList &htmlLines);
QString renderReplaceChatMessagesScript(const QStringList &htmlLines);
QString renderAppendChatLineScript(const QString &htmlLine);
