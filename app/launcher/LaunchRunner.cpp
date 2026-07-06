#include "launcher/LaunchRunner.h"

#include <QFileInfo>
#include <QProcess>

LaunchRunResult startLaunchProcess(const LaunchEntry &entry, const QString &address, int port, quint32 sessionId)
{
    LaunchRunResult result;
    result.executable = resolveLaunchPath(entry, entry.executable);

    if (!QFileInfo::exists(result.executable)) {
        result.status = LaunchRunStatus::MissingExecutable;
        return result;
    }

    const QString commandLine = substitutedCommandLine(entry, address, port, sessionId);
    const QStringList args = commandLine.trimmed().isEmpty() ? QStringList() : QProcess::splitCommand(commandLine);
    const QString workingDir = launchWorkingDirectory(entry, result.executable);
    if (!QProcess::startDetached(result.executable, args, workingDir)) {
        result.status = LaunchRunStatus::FailedToStart;
        return result;
    }

    recordLaunchLastUsed(entry.id);
    result.status = LaunchRunStatus::Started;
    return result;
}
