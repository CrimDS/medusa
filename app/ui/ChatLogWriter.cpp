#include "ui/ChatLogWriter.h"

#include "core/AppPaths.h"
#include "ui/ChatFormatting.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace {

QString defaultChatLogFolder()
{
    const QString fallback = QDir(QCoreApplication::applicationDirPath()).filePath(".Cache/GameCQ");
    const QString root = AppPaths::localDataRoot("GameCQ", fallback);
    return QDir(root).filePath("ChatLogs");
}

QString safeFileNamePart(QString value, const QString &fallback)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        value = fallback;
    }
    value.replace(QRegularExpression("[^A-Za-z0-9._@!#$%&()\\[\\]^`~-]+"), "_");
    value.replace(QRegularExpression("_+"), "_");
    return value.left(80);
}

} // namespace

ChatLogWriter::ChatLogWriter(QObject *parent)
    : QObject(parent)
{
}

void ChatLogWriter::setAccountState(const QString &profileName, quint32 sessionId)
{
    if (m_profileName == profileName && m_sessionId == sessionId) {
        return;
    }

    flush();
    m_profileName = profileName;
    m_sessionId = sessionId;
    m_sessionLogPath = (m_sessionId == 0 && m_profileName.isEmpty()) ? QString() : newSessionLogFilePath();
    m_writeFailed = false;
}

void ChatLogWriter::writeLine(const QString &line, quint32 seconds)
{
    if (!loggingEnabled() || line.trimmed().isEmpty()) {
        return;
    }

    const QString path = logFilePath();
    if (!m_pendingPath.isEmpty() && m_pendingPath != path) {
        flush();
    }

    m_pendingPath = path;
    m_pendingRecords.append(QString("[%1] %2\n").arg(logTime(seconds), line));
    if (!m_flushScheduled) {
        m_flushScheduled = true;
        QTimer::singleShot(750, this, &ChatLogWriter::flush);
    }
}

QStringList ChatLogWriter::recentLogLines(int maxLines)
{
    if (m_sessionId == 0 && m_profileName.isEmpty()) {
        return {};
    }

    flush();

    QDir dir(logFolder());
    if (!dir.exists()) {
        return {};
    }

    const QString account = safeFileNamePart(m_profileName, "offline");
    const QRegularExpression filePattern(QString(R"(^GameCQ_%1_(\d{4}-\d{2}-\d{2})(?:_(\d{2}-\d{2}-\d{2})(?:_\d+)?)?\.log$)")
                                             .arg(QRegularExpression::escape(account)));

    QList<QFileInfo> logFiles;
    const QFileInfoList entries = dir.entryInfoList({"GameCQ_*.log"}, QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (filePattern.match(entry.fileName()).hasMatch()) {
            logFiles.append(entry);
        }
    }
    std::sort(logFiles.begin(), logFiles.end(), [](const QFileInfo &left, const QFileInfo &right) {
        return left.fileName().compare(right.fileName(), Qt::CaseInsensitive) < 0;
    });

    QStringList lines;
    for (const QFileInfo &entry : logFiles) {
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        const auto match = filePattern.match(entry.fileName());
        QString sessionLabel = match.captured(1);
        if (!match.captured(2).isEmpty()) {
            sessionLabel += QString(" %1").arg(match.captured(2).replace('-', ':'));
        }
        lines.append(QString("--- Session log %1 ---").arg(sessionLabel));
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith('#')) {
                continue;
            }
            lines.append(line);
            while (maxLines > 0 && lines.size() > maxLines) {
                lines.removeFirst();
            }
        }
    }
    return lines;
}

void ChatLogWriter::flush()
{
    if (m_pendingPath.isEmpty() || m_pendingRecords.isEmpty()) {
        m_flushScheduled = false;
        return;
    }

    const QString path = m_pendingPath;
    const QStringList records = std::exchange(m_pendingRecords, {});
    m_pendingPath.clear();
    m_flushScheduled = false;

    QDir().mkpath(QFileInfo(path).absolutePath());
    const bool newFile = !QFileInfo::exists(path) || QFileInfo(path).size() == 0;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        if (!m_writeFailed) {
            m_writeFailed = true;
            if (m_errorCallback) {
                m_errorCallback(path);
            }
        }
        return;
    }

    if (newFile) {
        const QString header = QString("# GameCQ 1 ⅜ chat log\n# Account: %1\n# Session: %2\n# Created: %3\n\n")
                                   .arg(m_profileName.isEmpty() ? QString("offline") : m_profileName,
                                        QString::number(m_sessionId),
                                        logTime(0));
        file.write(header.toUtf8());
    }

    for (const QString &record : records) {
        file.write(record.toUtf8());
    }
}

void ChatLogWriter::resetWriteFailure()
{
    m_writeFailed = false;
}

void ChatLogWriter::setErrorCallback(std::function<void(const QString &path)> callback)
{
    m_errorCallback = std::move(callback);
}

bool ChatLogWriter::loggingEnabled() const
{
    if (m_sessionId == 0 && m_profileName.isEmpty()) {
        return false;
    }

    return QSettings().value("Chat/LoggingEnabled", false).toBool();
}

QString ChatLogWriter::logFolder() const
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QSettings().value("Chat/LogFolder", defaultChatLogFolder()).toString()));
}

QString ChatLogWriter::logFilePath() const
{
    if (!m_sessionLogPath.isEmpty()) {
        return m_sessionLogPath;
    }
    return newSessionLogFilePath();
}

QString ChatLogWriter::newSessionLogFilePath() const
{
    const QString folder = logFolder();
    const QString account = safeFileNamePart(m_profileName, "offline");
    const QString stamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
    return QDir(folder).filePath(QString("GameCQ_%1_%2_%3.log").arg(account, stamp, QString::number(m_sessionId)));
}
