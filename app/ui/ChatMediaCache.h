#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QTextDocument;

class ChatMediaCache final : public QObject {
    Q_OBJECT

public:
    explicit ChatMediaCache(QObject *parent = nullptr);

    bool request(const QUrl &source, const QUrl &resource);
    void addResources(QTextDocument *document) const;

signals:
    void imageReady();

private:
    QNetworkAccessManager *m_network = nullptr;
    QHash<QString, QImage> m_images;
    QSet<QString> m_pending;
};