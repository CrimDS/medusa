#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <functional>

class ChatLogWriter final : public QObject {
    Q_OBJECT

public:
    explicit ChatLogWriter(QObject *parent = nullptr);

    void setAccountState(const QString &profileName, quint32 sessionId);
    void writeLine(const QString &line, quint32 seconds = 0);
    QStringList recentLogLines(int maxLines);
    void flush();
    void resetWriteFailure();
    void setErrorCallback(std::function<void(const QString &path)> callback);

private:
    bool loggingEnabled() const;
    QString logFolder() const;
    QString logFilePath() const;
    QString newSessionLogFilePath() const;

    QString m_profileName;
    QString m_sessionLogPath;
    QString m_pendingPath;
    QStringList m_pendingRecords;
    std::function<void(const QString &path)> m_errorCallback;
    quint32 m_sessionId = 0;
    bool m_flushScheduled = false;
    bool m_writeFailed = false;
};
