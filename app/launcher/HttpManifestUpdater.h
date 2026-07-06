#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QUrl>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

class HttpManifestUpdater final : public QObject {
    Q_OBJECT

public:
    explicit HttpManifestUpdater(QObject *parent = nullptr);

    bool isRunning() const;
    // Starts the D12 updater. The updater compares the remote manifest with
    // installRoot, prefers a full archive for large installs when available,
    // and otherwise downloads missing/changed files in bounded parallel batches.
    void start(const QUrl &manifestUrl, const QString &installRoot);
    void cancel();

signals:
    void statusChanged(const QString &status);
    void progressChanged(int completed, int total, const QString &path);
    void finished(bool success, const QString &message);

private:
    struct ManifestFile {
        QString path;
        QString url;
        QString sha256;
        qint64 size = 0;
    };

    struct ArchiveInfo {
        QString url;
        QString sha256;
        qint64 size = 0;
    };

    void reset();
    void failUpdate(const QString &message);
    void abortActiveReplies(QNetworkReply *except = nullptr);
    void clearArchiveDownload(bool removeFile);
    bool finishIfCancelled();
    void fetchManifest();
    void handleManifestReply(QNetworkReply *reply);
    bool queueOutdatedFiles(const QByteArray &manifestBytes);
    bool isCurrent(const ManifestFile &file) const;
    bool shouldUseFullArchive() const;
    void downloadFullArchive();
    void handleArchiveReadyRead(QNetworkReply *reply);
    void handleArchiveReply(QNetworkReply *reply);
    bool verifyArchive() const;
    void extractFullArchive();
    void handleExtractFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void downloadNext();
    void startFileDownload(const ManifestFile &file);
    void handleFileReply(QNetworkReply *reply, const ManifestFile &file);
    QString localPath(const ManifestFile &file) const;
    QUrl fileUrl(const ManifestFile &file) const;
    QUrl archiveUrl() const;

    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_manifestReply = nullptr;
    QNetworkReply *m_archiveReply = nullptr;
    QFile *m_archiveFile = nullptr;
    QProcess *m_extractProcess = nullptr;
    QHash<QNetworkReply *, ManifestFile> m_activeDownloads;
    QUrl m_manifestUrl;
    QString m_installRoot;
    ArchiveInfo m_fullArchive;
    QString m_archiveTempPath;
    QQueue<ManifestFile> m_pending;
    qint64 m_pendingBytes = 0;
    qint64 m_archiveReceived = 0;
    int m_total = 0;
    int m_completed = 0;
    bool m_running = false;
    bool m_cancelRequested = false;
};
