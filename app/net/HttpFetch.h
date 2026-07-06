#pragma once

#include <QByteArray>

class QObject;
class QString;
class QUrl;

QByteArray fetchUrlBytes(QObject *parent, const QUrl &url, QString *error, int timeoutMs = 15000);
