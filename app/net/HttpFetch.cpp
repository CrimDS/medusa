#include "net/HttpFetch.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariant>

QByteArray fetchUrlBytes(QObject *parent, const QUrl &url, QString *error, int timeoutMs)
{
    if (error) {
        error->clear();
    }

    QNetworkAccessManager network(parent);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "GameCQ/1.3.8");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = network.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&]() {
        if (reply->isRunning()) {
            reply->abort();
        }
        loop.quit();
    });

    timeout.start(timeoutMs);
    loop.exec();

    QByteArray bytes;
    if (reply->error() == QNetworkReply::NoError) {
        bytes = reply->readAll();
    } else if (error) {
        const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QString status = statusCode.isValid() ? QString("HTTP %1").arg(statusCode.toInt()) : QString();
        const QString reason = reply->errorString();
        const QString effectiveUrl = reply->url().toString(QUrl::RemoveUserInfo);
        QStringList parts;
        if (!status.isEmpty()) {
            parts << status;
        }
        if (!reason.isEmpty()) {
            parts << reason;
        }
        if (!effectiveUrl.isEmpty()) {
            parts << effectiveUrl;
        }
        *error = parts.join(" - ");
    }
    reply->deleteLater();
    return bytes;
}
