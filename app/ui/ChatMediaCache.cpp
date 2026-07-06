#include "ui/ChatMediaCache.h"

#include "ui/DiscordChatMedia.h"

#include <QBuffer>
#include <QDebug>
#include <QFutureWatcher>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextDocument>
#include <QUrlQuery>
#include <QtConcurrent>

namespace {

constexpr int kMaxChatMediaCacheItems = 128;

struct ChatMediaDecodeResult {
    QString key;
    QUrl resource;
    QImage image;
};

QUrl normalizedFetchUrl(QUrl fetchUrl)
{
    if ((fetchUrl.host().endsWith("discordapp.com", Qt::CaseInsensitive)
         || fetchUrl.host().endsWith("discordapp.net", Qt::CaseInsensitive))
        && fetchUrl.path().endsWith(".webp", Qt::CaseInsensitive)) {
        QString path = fetchUrl.path();
        path.chop(5);
        fetchUrl.setPath(path + ".png");
    }
    if (fetchUrl.host().endsWith("discordapp.net", Qt::CaseInsensitive)) {
        QUrlQuery query(fetchUrl);
        if (query.hasQueryItem("format")) {
            query.removeAllQueryItems("format");
            query.addQueryItem("format", "png");
            fetchUrl.setQuery(query);
        }
    }
    return fetchUrl;
}

ChatMediaDecodeResult decodeChatMedia(const QByteArray &data, const QString &key, const QUrl &resource)
{
    ChatMediaDecodeResult result;
    result.key = key;
    result.resource = resource;

    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize original = reader.size();
    if (original.isValid() && (original.width() > 8192 || original.height() > 8192)) {
        return result;
    }
    const QSize bounded = original.isValid()
        ? original.scaled(QSize(360, 220), Qt::KeepAspectRatio)
        : QSize();
    if (bounded.isValid() && bounded != original) {
        reader.setScaledSize(bounded);
    }
    QImage image = reader.read();
    if (image.isNull() || image.width() > 8192 || image.height() > 8192) {
        return result;
    }
    if (image.width() > 360 || image.height() > 220) {
        image = image.scaled(QSize(360, 220), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    result.image = image;
    return result;
}

} // namespace

ChatMediaCache::ChatMediaCache(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

bool ChatMediaCache::request(const QUrl &source, const QUrl &resource)
{
    if (!source.isValid() || source.scheme() != "https" || !isTrustedDiscordMediaHost(source.host())) {
        return false;
    }

    const QString key = resource.toString();
    if (m_images.contains(key) || m_pending.contains(key)) {
        return false;
    }
    m_pending.insert(key);

    QNetworkRequest request(normalizedFetchUrl(source));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    request.setRawHeader("User-Agent", "GameCQ/1.3.8 Discord media preview");
    request.setRawHeader("Accept", "image/png,image/gif,image/jpeg,*/*;q=0.5");
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, resource]() {
        m_pending.remove(key);
        const QByteArray data = reply->readAll();
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QString errorString = reply->errorString();
        const bool acceptable = reply->error() == QNetworkReply::NoError
            && reply->url().scheme() == "https"
            && isTrustedDiscordMediaHost(reply->url().host())
            && !data.isEmpty()
            && data.size() <= 8 * 1024 * 1024;
        reply->deleteLater();
        if (!acceptable) {
            qWarning().noquote() << "Discord media preview failed:" << status.toInt() << errorString;
            return;
        }

        auto *watcher = new QFutureWatcher<ChatMediaDecodeResult>(this);
        connect(watcher, &QFutureWatcher<ChatMediaDecodeResult>::finished, this, [this, watcher]() {
            const ChatMediaDecodeResult result = watcher->result();
            watcher->deleteLater();
            if (result.image.isNull()) {
                return;
            }

            if (m_images.size() >= kMaxChatMediaCacheItems && !m_images.contains(result.key)) {
                m_images.erase(m_images.begin());
            }
            m_images.insert(result.key, result.image);
            emit imageReady();
        });
        watcher->setFuture(QtConcurrent::run([data, key, resource]() {
            return decodeChatMedia(data, key, resource);
        }));
    });

    return true;
}

void ChatMediaCache::addResources(QTextDocument *document) const
{
    if (!document) {
        return;
    }

    for (auto it = m_images.cbegin(); it != m_images.cend(); ++it) {
        document->addResource(QTextDocument::ImageResource, QUrl(it.key()), it.value());
    }
}
