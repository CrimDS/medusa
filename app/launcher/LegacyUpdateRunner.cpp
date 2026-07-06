#include "launcher/LegacyUpdateRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

namespace {

constexpr auto kD9MirrorAddress = "mirror-server.palestar.com";
constexpr int kD9MirrorPort = 9101;
constexpr auto kLegacyD9UpdateTool = "GameCQLegacyUpdate.exe";
constexpr auto kLegacyMirrorClientX86Tool = "MirrorClientD.exe";

} // namespace

QString legacyD9UpdateToolFileName()
{
    return kLegacyD9UpdateTool;
}

QString findLegacyD9UpdateToolPath()
{
    const QDir appDir(QCoreApplication::applicationDirPath());

    const QString bundledHelper = appDir.filePath(kLegacyD9UpdateTool);
    if (QFileInfo::exists(bundledHelper)) {
        return QDir::cleanPath(bundledHelper);
    }

    const QString bundledX86 = appDir.filePath(kLegacyMirrorClientX86Tool);
    if (QFileInfo::exists(bundledX86)) {
        return QDir::cleanPath(bundledX86);
    }

    QDir search(appDir);
    for (int i = 0; i < 6; ++i) {
        const QString builtHelper = search.filePath("build/legacy_updater_x86/Debug/GameCQLegacyUpdate.exe");
        if (QFileInfo::exists(builtHelper)) {
            return QDir::cleanPath(builtHelper);
        }
        const QString builtHelperRelease = search.filePath("build/legacy_updater_x86/Release/GameCQLegacyUpdate.exe");
        if (QFileInfo::exists(builtHelperRelease)) {
            return QDir::cleanPath(builtHelperRelease);
        }
        const QString devTreeX86 = search.filePath("gamecq/Bin/MirrorClientD.exe");
        if (QFileInfo::exists(devTreeX86)) {
            return QDir::cleanPath(devTreeX86);
        }
        if (!search.cdUp()) {
            break;
        }
    }

    return {};
}

QString legacyD9MirrorStatusDetail(const QString &toolPath)
{
    return QString("legacy GCQL mirror %1:%2 via %3")
        .arg(kD9MirrorAddress)
        .arg(kD9MirrorPort)
        .arg(QFileInfo(toolPath).fileName());
}

bool legacyUpdateUsesClientHelper(const QString &toolPath)
{
    return QFileInfo(toolPath).fileName().compare(kLegacyD9UpdateTool, Qt::CaseInsensitive) == 0;
}

void configureLegacyUpdateProcess(QProcess *process, const QString &toolPath, const QString &installRoot, quint32 sessionId)
{
    if (!process) {
        return;
    }

    const bool usingClientUpdateHelper = legacyUpdateUsesClientHelper(toolPath);
    QStringList arguments = {QDir::toNativeSeparators(installRoot), kD9MirrorAddress, QString::number(kD9MirrorPort)};
    if (usingClientUpdateHelper && sessionId != 0) {
        arguments << QString::number(sessionId);
    }

    process->setProgram(toolPath);
    process->setArguments(arguments);
    process->setWorkingDirectory(usingClientUpdateHelper ? installRoot : QFileInfo(toolPath).absolutePath());
    process->setProcessChannelMode(usingClientUpdateHelper ? QProcess::MergedChannels : QProcess::ForwardedChannels);
}

QString latestLegacyUpdateOutputLine(QProcess *process, int maxLength)
{
    if (!process) {
        return {};
    }

    const QString output = QString::fromLocal8Bit(process->readAll()).trimmed();
    const QStringList lines = output.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    return lines.isEmpty() ? QString() : lines.constLast().left(maxLength);
}

QString legacyUpdateStatusForLine(const QString &line)
{
    const int percent = legacyUpdatePercentForLine(line);
    if (percent >= 0) {
        return QString("Downloading legacy files... %1%").arg(percent);
    }
    if (line.startsWith("DOWNLOAD:") || line.startsWith("FILE:") || line.startsWith("PROGRESS:")
        || line.contains("Downloading", Qt::CaseInsensitive)) {
        return "Downloading legacy files... 0%";
    }
    return "Updating legacy client...";
}

int legacyUpdatePercentForLine(const QString &line)
{
    static const QRegularExpression progressExpression("(?:PROGRESS|FILE):.*?(\\d+)\\s*/\\s*(\\d+)");
    const QRegularExpressionMatch match = progressExpression.match(line);
    if (!match.hasMatch()) {
        return -1;
    }

    bool bytesOk = false;
    bool totalOk = false;
    const double bytes = match.captured(1).toDouble(&bytesOk);
    const double total = match.captured(2).toDouble(&totalOk);
    if (!bytesOk || !totalOk || total <= 0.0) {
        return -1;
    }

    return qBound(0, static_cast<int>((bytes * 100.0) / total), 100);
}

QString latestLegacyUpdateDetail(const QString &processOutput, const QString &fallback)
{
    const QStringList outputLines = processOutput.trimmed().split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    return outputLines.isEmpty() ? fallback : outputLines.constLast();
}
