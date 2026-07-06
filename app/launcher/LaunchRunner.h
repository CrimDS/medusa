#pragma once

#include "launcher/LaunchCatalog.h"

#include <QString>
#include <QtGlobal>

enum class LaunchRunStatus {
    Started,
    MissingExecutable,
    FailedToStart,
};

struct LaunchRunResult {
    LaunchRunStatus status = LaunchRunStatus::FailedToStart;
    QString executable;
};

LaunchRunResult startLaunchProcess(const LaunchEntry &entry, const QString &address, int port, quint32 sessionId);
