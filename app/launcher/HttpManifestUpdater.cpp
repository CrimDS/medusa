#include "launcher/HttpManifestUpdater.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <limits>

namespace {

constexpr int kMaxParallelDownloads = 12;
constexpr qint64 kFullArchiveThresholdBytes = 64ll * 1024ll * 1024ll;
constexpr int kFullArchiveThresholdFiles = 96;
constexpr auto kUserAgent = "DarkSpaceLauncher/0.1 GameCQ/1.3.8";

QString formatMiB(qint64 bytes)
{
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1);
}

QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, kUserAgent);
    request.setRawHeader("Accept", "application/json,text/plain,*/*");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QString safeManifestPath(const QString &rawPath)
{
    const QString path = QDir::fromNativeSeparators(rawPath.trimmed());
    // Manifests are remote data. Reject absolute paths and traversal before
    // they are combined with the install root.
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path == "." || path == ".." || path.startsWith("../") || path.contains("/../") || path.endsWith("/..")) {
        return {};
    }

    const QString cleaned = QDir::cleanPath(path);
    if (cleaned.isEmpty() || cleaned == "." || cleaned == ".." || cleaned.startsWith("../") || cleaned.contains("/../")) {
        return {};
    }

    return cleaned;
}

QByteArray quoteManifestNumbersForQt(const QByteArray &manifestBytes)
{
    QString text = QString::fromUtf8(manifestBytes);
    static const QRegularExpression sizeExpression(R"(("[A-Za-z0-9_\-]+"\s*:\s*)(-?\d+(?:\.\d+)?(?:[eE][+\-]?\d+)?))");
    qsizetype offset = 0;
    while (true) {
        const QRegularExpressionMatch match = sizeExpression.match(text, offset);
        if (!match.hasMatch()) {
            break;
        }

        const QString quoted = match.captured(1) + "\"" + match.captured(2) + "\"";
        text.replace(match.capturedStart(), match.capturedLength(), quoted);
        offset = match.capturedStart() + quoted.size();
    }
    return text.toUtf8();
}

qint64 manifestSizeValue(const QJsonValue &value)
{
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().trimmed().toLongLong(&ok);
        return ok && parsed > 0 ? parsed : 0;
    }
    if (value.isDouble()) {
        const double parsed = value.toDouble();
        if (parsed > 0.0 && parsed <= static_cast<double>(std::numeric_limits<qint64>::max())) {
            return static_cast<qint64>(parsed);
        }
    }
    return 0;
}

QString manifestHashValue(const QJsonObject &object)
{
    const QString sha256 = object.value("sha256").toString().trimmed();
    if (!sha256.isEmpty()) {
        return sha256.toLower();
    }

    const QString hash = object.value("hash").toString().trimmed();
    if (!hash.isEmpty()) {
        return hash.toLower();
    }

    return object.value("checksum").toString().trimmed().toLower();
}

} // namespace

HttpManifestUpdater::HttpManifestUpdater(QObject *parent)
    : QObject(parent),
      m_network(new QNetworkAccessManager(this))
{
}

bool HttpManifestUpdater::isRunning() const
{
    return m_running;
}

void HttpManifestUpdater::start(const QUrl &manifestUrl, const QString &installRoot)
{
    if (m_running) {
        emit statusChanged("Update already running.");
        return;
    }

    if (!manifestUrl.isValid() || manifestUrl.scheme().isEmpty()) {
        emit finished(false, "D12 manifest URL is not valid.");
        return;
    }

    if (installRoot.trimmed().isEmpty()) {
        emit finished(false, "D12 install folder is not configured.");
        return;
    }

    m_manifestUrl = manifestUrl;
    m_installRoot = QDir::cleanPath(installRoot);
    m_running = true;
    m_cancelRequested = false;
    m_total = 0;
    m_completed = 0;
    m_pendingBytes = 0;
    m_archiveReceived = 0;
    m_fullArchive = {};
    m_pending.clear();

    if (!QDir().mkpath(m_installRoot)) {
        reset();
        emit finished(false, QString("Could not create %1.").arg(QDir::toNativeSeparators(m_installRoot)));
        return;
    }

    fetchManifest();
}

void HttpManifestUpdater::cancel()
{
    if (!m_running) {
        return;
    }

    m_cancelRequested = true;
    if (m_manifestReply) {
        m_manifestReply->abort();
    }

    if (m_archiveReply) {
        m_archiveReply->abort();
    }

    const QList<QNetworkReply *> replies = m_activeDownloads.keys();
    for (QNetworkReply *reply : replies) {
        if (reply) {
            reply->abort();
        }
    }

    if (m_extractProcess) {
        m_extractProcess->kill();
    }

    if (!m_manifestReply && !m_archiveReply && m_activeDownloads.isEmpty() && !m_extractProcess) {
        finishIfCancelled();
    }
}

void HttpManifestUpdater::reset()
{
    m_manifestReply = nullptr;
    m_archiveReply = nullptr;
    m_extractProcess = nullptr;
    m_activeDownloads.clear();
    clearArchiveDownload(false);
    m_fullArchive = {};
    m_pending.clear();
    m_total = 0;
    m_completed = 0;
    m_pendingBytes = 0;
    m_archiveReceived = 0;
    m_running = false;
    m_cancelRequested = false;
}

void HttpManifestUpdater::failUpdate(const QString &message)
{
    abortActiveReplies();
    clearArchiveDownload(true);
    reset();
    emit finished(false, message);
}

void HttpManifestUpdater::abortActiveReplies(QNetworkReply *except)
{
    if (m_manifestReply && m_manifestReply != except) {
        QNetworkReply *reply = m_manifestReply;
        m_manifestReply = nullptr;
        reply->abort();
    }

    if (m_archiveReply && m_archiveReply != except) {
        QNetworkReply *reply = m_archiveReply;
        m_archiveReply = nullptr;
        reply->abort();
    }

    const QList<QNetworkReply *> replies = m_activeDownloads.keys();
    for (QNetworkReply *reply : replies) {
        if (!reply || reply == except) {
            continue;
        }
        m_activeDownloads.remove(reply);
        reply->abort();
    }

    if (m_extractProcess) {
        QProcess *process = m_extractProcess;
        m_extractProcess = nullptr;
        process->kill();
    }
}

void HttpManifestUpdater::clearArchiveDownload(bool removeFile)
{
    if (m_archiveFile) {
        if (m_archiveFile->isOpen()) {
            m_archiveFile->close();
        }
        delete m_archiveFile;
        m_archiveFile = nullptr;
    }

    if (removeFile && !m_archiveTempPath.isEmpty()) {
        QFile::remove(m_archiveTempPath);
    }

    if (removeFile) {
        m_archiveTempPath.clear();
    }
}

bool HttpManifestUpdater::finishIfCancelled()
{
    if (!m_cancelRequested) {
        return false;
    }

    clearArchiveDownload(true);
    reset();
    emit finished(false, "D12 update cancelled.");
    return true;
}

void HttpManifestUpdater::fetchManifest()
{
    emit statusChanged("Checking D12 manifest...");

    auto *reply = m_network->get(makeRequest(m_manifestUrl));
    m_manifestReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleManifestReply(reply);
    });
}

void HttpManifestUpdater::handleManifestReply(QNetworkReply *reply)
{
    if (m_manifestReply == reply) {
        m_manifestReply = nullptr;
    }
    reply->deleteLater();

    if (!m_running) {
        return;
    }

    if (finishIfCancelled()) {
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const QString message = QString("Failed to fetch manifest: %1").arg(reply->errorString());
        failUpdate(message);
        return;
    }

    if (!queueOutdatedFiles(reply->readAll())) {
        return;
    }
    if (!m_running) {
        return;
    }

    if (m_pending.isEmpty()) {
        reset();
        emit finished(true, "D12 client is already up to date.");
        return;
    }

    m_total = m_pending.size();
    if (shouldUseFullArchive()) {
        emit statusChanged(QString("Using full D12 archive for %1 files (%2 MB).").arg(m_total).arg(formatMiB(m_pendingBytes)));
        downloadFullArchive();
        return;
    }

    emit statusChanged(QString("Updating D12 client: %1 files.").arg(m_total));
    downloadNext();
}

bool HttpManifestUpdater::queueOutdatedFiles(const QByteArray &manifestBytes)
{
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error == QJsonParseError::IllegalNumber) {
        parseError = {};
        document = QJsonDocument::fromJson(quoteManifestNumbersForQt(manifestBytes), &parseError);
    }
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        failUpdate(QString("Manifest could not be read: %1.").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonValue fullValue = root.value("full");
    // A manifest can provide either a structured archive object or a legacy
    // string URL. The archive is only used later when the patch size warrants it.
    if (fullValue.isObject()) {
        const QJsonObject full = fullValue.toObject();
        m_fullArchive.url = full.value("url").toString();
        m_fullArchive.sha256 = full.value("sha256").toString().toLower();
        m_fullArchive.size = manifestSizeValue(full.value("size"));
    } else if (fullValue.isString()) {
        m_fullArchive.url = fullValue.toString();
    }

    const QJsonValue filesValue = root.value("files");
    QJsonArray files;
    if (filesValue.isArray()) {
        files = filesValue.toArray();
    } else if (filesValue.isObject()) {
        const QJsonObject filesObject = filesValue.toObject();
        for (auto it = filesObject.constBegin(); it != filesObject.constEnd(); ++it) {
            QJsonObject fileObject;
            if (it.value().isObject()) {
                fileObject = it.value().toObject();
            } else if (it.value().isString()) {
                fileObject.insert("sha256", it.value().toString());
            }
            if (!fileObject.contains("path")) {
                fileObject.insert("path", it.key());
            }
            files.append(fileObject);
        }
    }

    if (files.isEmpty()) {
        const QString message = root.value("message").toString().trimmed();
        if (!message.isEmpty()) {
            failUpdate(QString("Manifest server replied: %1").arg(message));
            return false;
        }
        failUpdate("Manifest did not contain any files.");
        return false;
    }

    emit statusChanged(QString("Checking %1 D12 files...").arg(files.size()));
    for (const QJsonValue &value : files) {
        const QJsonObject object = value.toObject();
        ManifestFile file;
        file.path = safeManifestPath(object.value("path").toString());
        file.url = object.value("url").toString(file.path);
        file.sha256 = manifestHashValue(object);
        file.size = manifestSizeValue(object.value("size"));

        if (file.path.isEmpty() || localPath(file).isEmpty()) {
            // Skip invalid remote paths rather than failing the whole manifest;
            // localPath performs the final install-root containment check.
            continue;
        }

        if (!isCurrent(file)) {
            m_pending.enqueue(file);
            if (file.size > 0) {
                m_pendingBytes += file.size;
            }
        }
    }

    return true;
}

bool HttpManifestUpdater::isCurrent(const ManifestFile &file) const
{
    const QString path = localPath(file);
    if (path.isEmpty()) {
        return true;
    }

    QFile local(path);
    if (!local.exists() || !local.open(QIODevice::ReadOnly)) {
        return false;
    }

    if (file.size > 0 && local.size() != file.size) {
        return false;
    }

    if (file.sha256.isEmpty()) {
        return true;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&local)) {
        return false;
    }

    return QString::fromLatin1(hash.result().toHex()) == file.sha256;
}

bool HttpManifestUpdater::shouldUseFullArchive() const
{
    // Thousands of tiny asset files are much faster to install through one
    // archive than through per-file HTTP requests.
    return !m_fullArchive.url.trimmed().isEmpty()
           && (m_pending.size() >= kFullArchiveThresholdFiles || m_pendingBytes >= kFullArchiveThresholdBytes);
}

void HttpManifestUpdater::downloadFullArchive()
{
    if (!m_running || finishIfCancelled()) {
        return;
    }

    const QString downloadRoot = QDir(m_installRoot).filePath(".gamecq_download");
    if (!QDir().mkpath(downloadRoot)) {
        failUpdate(QString("Could not create %1.").arg(QDir::toNativeSeparators(downloadRoot)));
        return;
    }

    clearArchiveDownload(true);
    m_archiveTempPath = QDir(downloadRoot).filePath("_darkspace_update.zip");
    m_archiveFile = new QFile(m_archiveTempPath);
    if (!m_archiveFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failUpdate(QString("Could not write %1.").arg(QDir::toNativeSeparators(m_archiveTempPath)));
        return;
    }

    m_archiveReceived = 0;
    emit statusChanged(QString("Downloading full D12 update (%1 MB).").arg(formatMiB(m_fullArchive.size)));
    emit progressChanged(0, 100, "Full archive");

    auto *reply = m_network->get(makeRequest(archiveUrl()));
    m_archiveReply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        handleArchiveReadyRead(reply);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 expected = total > 0 ? total : m_fullArchive.size;
        const int percent = expected > 0 ? static_cast<int>((received * 100) / expected) : 0;
        emit progressChanged(percent, 100, QString("Full archive %1 / %2 MB").arg(formatMiB(received), formatMiB(expected)));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleArchiveReply(reply);
    });
}

void HttpManifestUpdater::handleArchiveReadyRead(QNetworkReply *reply)
{
    if (!m_running || !m_archiveFile || m_archiveReply != reply) {
        return;
    }

    const QByteArray data = reply->readAll();
    if (data.isEmpty()) {
        return;
    }

    const qint64 written = m_archiveFile->write(data);
    if (written != data.size()) {
        failUpdate("Failed to write the D12 full update archive.");
        return;
    }
    m_archiveReceived += written;
}

void HttpManifestUpdater::handleArchiveReply(QNetworkReply *reply)
{
    if (m_archiveReply == reply) {
        m_archiveReply = nullptr;
    }
    handleArchiveReadyRead(reply);
    reply->deleteLater();

    if (m_archiveFile && m_archiveFile->isOpen()) {
        m_archiveFile->close();
    }

    if (!m_running) {
        return;
    }

    if (finishIfCancelled()) {
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        failUpdate(QString("Failed to download full D12 update: %1").arg(reply->errorString()));
        return;
    }

    if (m_fullArchive.size > 0 && QFileInfo(m_archiveTempPath).size() != m_fullArchive.size) {
        failUpdate("Downloaded D12 full update with unexpected size.");
        return;
    }

    if (!verifyArchive()) {
        failUpdate("Downloaded D12 full update failed hash verification.");
        return;
    }

    extractFullArchive();
}

bool HttpManifestUpdater::verifyArchive() const
{
    if (m_fullArchive.sha256.isEmpty()) {
        return true;
    }

    QFile file(m_archiveTempPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex()) == m_fullArchive.sha256;
}

void HttpManifestUpdater::extractFullArchive()
{
    const QString tar = QStandardPaths::findExecutable("tar.exe");
    if (tar.isEmpty()) {
        failUpdate("tar.exe was not found; Windows 10 1803 or newer is required for full D12 archive extraction.");
        return;
    }

    emit statusChanged("Extracting full D12 update...");
    emit progressChanged(100, 100, "Extracting full archive");

    m_extractProcess = new QProcess(this);
    m_extractProcess->setProgram(tar);
    m_extractProcess->setArguments({"-xf", m_archiveTempPath, "-C", m_installRoot});
    m_extractProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_extractProcess, &QProcess::finished, this, &HttpManifestUpdater::handleExtractFinished);
    connect(m_extractProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (m_running && error == QProcess::FailedToStart) {
            failUpdate("Failed to start tar.exe for D12 full update extraction.");
        }
    });
    m_extractProcess->start();
}

void HttpManifestUpdater::handleExtractFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess *process = qobject_cast<QProcess *>(sender());
    if (process && m_extractProcess == process) {
        m_extractProcess = nullptr;
    }
    const QString output = process ? QString::fromLocal8Bit(process->readAll()).trimmed() : QString();
    if (process) {
        process->deleteLater();
    }

    if (!m_running) {
        return;
    }

    if (finishIfCancelled()) {
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        failUpdate(QString("D12 full update extraction failed%1.")
                       .arg(output.isEmpty() ? QString() : QString(": %1").arg(output.left(240))));
        return;
    }

    clearArchiveDownload(true);
    reset();
    emit finished(true, "D12 client update complete.");
}

void HttpManifestUpdater::downloadNext()
{
    if (!m_running || finishIfCancelled()) {
        return;
    }

    while (!m_pending.isEmpty() && m_activeDownloads.size() < kMaxParallelDownloads) {
        startFileDownload(m_pending.dequeue());
    }

    if (m_pending.isEmpty() && m_activeDownloads.isEmpty()) {
        reset();
        emit finished(true, "D12 client update complete.");
        return;
    }
}

void HttpManifestUpdater::startFileDownload(const ManifestFile &file)
{
    emit progressChanged(m_completed, m_total, file.path);
    emit statusChanged(QString("Downloading D12 client: %1/%2 complete, %3 active.")
                           .arg(m_completed)
                           .arg(m_total)
                           .arg(m_activeDownloads.size() + 1));

    auto *reply = m_network->get(makeRequest(fileUrl(file)));
    m_activeDownloads.insert(reply, file);
    connect(reply, &QNetworkReply::finished, this, [this, reply, file]() {
        handleFileReply(reply, file);
    });
}

void HttpManifestUpdater::handleFileReply(QNetworkReply *reply, const ManifestFile &file)
{
    m_activeDownloads.remove(reply);
    reply->deleteLater();

    if (!m_running) {
        return;
    }

    if (finishIfCancelled()) {
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const QString message = QString("Failed to download %1: %2").arg(file.path, reply->errorString());
        failUpdate(message);
        return;
    }

    const QByteArray data = reply->readAll();
    if (file.size > 0 && data.size() != file.size) {
        const QString message = QString("Downloaded %1 with unexpected size.").arg(file.path);
        failUpdate(message);
        return;
    }

    if (!file.sha256.isEmpty()) {
        const QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
        if (QString::fromLatin1(hash) != file.sha256) {
            const QString message = QString("Downloaded %1 failed hash verification.").arg(file.path);
            failUpdate(message);
            return;
        }
    }

    const QString path = localPath(file);
    if (path.isEmpty()) {
        const QString message = QString("Manifest path %1 is outside the install folder.").arg(file.path);
        failUpdate(message);
        return;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) {
        const QString message = QString("Could not write %1.").arg(path);
        failUpdate(message);
        return;
    }

    ++m_completed;
    emit progressChanged(m_completed, m_total, file.path);
    QTimer::singleShot(0, this, &HttpManifestUpdater::downloadNext);
}

QString HttpManifestUpdater::localPath(const ManifestFile &file) const
{
    const QDir rootDir(m_installRoot);
    const QString root = QDir::cleanPath(rootDir.absolutePath());
    const QString path = QDir::cleanPath(QFileInfo(rootDir.filePath(file.path)).absoluteFilePath());
    // Defense in depth for manifest paths: every output path must stay inside
    // the configured install root after QFileInfo resolves it.
    if (path.compare(root, Qt::CaseInsensitive) == 0 || path.startsWith(root + '/', Qt::CaseInsensitive)) {
        return path;
    }

    return {};
}

QUrl HttpManifestUpdater::fileUrl(const ManifestFile &file) const
{
    return m_manifestUrl.resolved(QUrl(file.url));
}

QUrl HttpManifestUpdater::archiveUrl() const
{
    return m_manifestUrl.resolved(QUrl(m_fullArchive.url));
}
