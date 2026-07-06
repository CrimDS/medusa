#pragma once

#include <QString>
#include <QtGlobal>

class QWidget;

void showGameCQAboutDialog(QWidget *parent);
void showChatCommandsHelpDialog(QWidget *parent,
                                quint32 sessionId,
                                const QString &profileName,
                                quint32 profileFlags,
                                bool canModerate,
                                bool canUseStaff);