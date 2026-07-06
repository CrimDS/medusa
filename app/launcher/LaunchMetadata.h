#pragma once

#include "launcher/LaunchCatalog.h"
#include "net/MetaClientTypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

class QWidget;

struct SteamLocalApp {
    QString appId;
    QString name;
    QString installDir;
    QString manifestPath;

    bool isValid() const { return !appId.isEmpty(); }
};

struct SteamRemoteMetadata {
    QString appId;
    QString name;
    QString shortDescription;
    QString headerImageUrl;
    QString capsuleImageUrl;
    QString backgroundImageUrl;
    QJsonArray newsItems;
    QJsonArray screenshots;

    bool hasStoreData() const { return !name.isEmpty() || !shortDescription.isEmpty() || !headerImageUrl.isEmpty(); }
};

struct SteamSearchResult {
    QString appId;
    QString name;
};

struct SteamSearchBatch {
    QString query;
    QString error;
    QList<SteamSearchResult> matches;
};

struct SteamIdentifyResult {
    SteamRemoteMetadata metadata;
    QString error;
    QString bannerPath;
    QString capsulePath;
};

struct DarkSpaceUpdatesResult {
    QJsonArray items;
    QString error;
};

struct SteamServerProbeResult {
    QString launchId;
    QString appId;
    QString error;
    QList<ServerInfo> servers;
};

QJsonObject readLaunchJsonObject(const QString &path);
QString compactPlainText(QString text, int maxLength = 260);
QString plainLaunchHtmlText(const QString &html, int maxLength = 1600);
bool isLikelyEnglishSteamNews(const QJsonObject &item);
QJsonArray parseDarkSpaceDevelopmentLog(const QByteArray &bytes, const QString &logUrl);
DarkSpaceUpdatesResult fetchDarkSpaceUpdates(const QUrl &url, int timeoutMs = 10000);

SteamLocalApp detectSteamAppForExecutable(const QString &executable);
SteamRemoteMetadata fetchSteamRemoteMetadata(QWidget *parent, const QString &appId, QString *error);
QList<SteamSearchResult> steamAppListFromBytes(const QByteArray &bytes);
QList<SteamSearchResult> steamAppList(QWidget *parent, QString *error);
QList<SteamSearchResult> steamStoreSearch(QWidget *parent, const QString &query, QString *error);
QList<SteamSearchResult> steamSearchMatches(QWidget *parent, const QString &query, QString *error);
QString downloadedLaunchArtworkPath(QWidget *parent, const LaunchEntry &entry, const QString &urlText, const QString &kind);
void writeSteamMetadataFiles(const LaunchEntry &entry, const SteamRemoteMetadata &metadata);
QString launchSteamAppId(const LaunchEntry &entry);
QList<ServerInfo> querySteamMasterServers(const QString &appId, QString *error, int maxServers = 80);
