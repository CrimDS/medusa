#pragma once

#include <QString>
#include <QtGlobal>

class QProcess;

QString legacyD9UpdateToolFileName();
QString findLegacyD9UpdateToolPath();
QString legacyD9MirrorStatusDetail(const QString &toolPath);
bool legacyUpdateUsesClientHelper(const QString &toolPath);
void configureLegacyUpdateProcess(QProcess *process, const QString &toolPath, const QString &installRoot, quint32 sessionId);
QString latestLegacyUpdateOutputLine(QProcess *process, int maxLength = 240);
QString legacyUpdateStatusForLine(const QString &line);
int legacyUpdatePercentForLine(const QString &line);
QString latestLegacyUpdateDetail(const QString &processOutput, const QString &fallback);
