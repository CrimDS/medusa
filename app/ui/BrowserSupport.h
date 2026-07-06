#pragma once

#include <QUrl>

#include "net/MetaClientTypes.h"

QString browserFallbackHtml(const QUrl &url);
QString browserLoadTroubleHtml(const QUrl &url);
QString browserStartHtml();
bool isDarkSpaceWebUrl(const QUrl &url);
bool isBrowserStartupUrl(const QUrl &url);
QUrl browserDisplayUrl(QUrl url);
bool isDarkSpaceHomeLikeUrl(const QUrl &url);
void configureWebEngineProfile();

QUrl browserUrlForSession(const QString &configuredUrl, const QString &fallbackUrl, quint32 sessionId);
QUrl browserHomeUrl(const GameLinks &links, quint32 sessionId);
QUrl browserProfileUrl(const GameLinks &links, quint32 sessionId, quint32 userId);
QUrl browserUrlWithSession(const QUrl &url, quint32 sessionId);
bool shouldRefreshBrowserForSession(bool hasBrowserView, const QString &currentUrl, quint32 sessionId);
bool isBrowserAtStartupOrHome(const QString &currentUrl);
QString darkSpaceHomeUrlString();
QString darkSpaceNewsUrlString();
QString darkSpaceForumUrlString();
QString darkSpaceDownloadsUrlString();
QString darkSpaceManualUrlString();
