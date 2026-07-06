#include "launcher/LaunchMetadata.h"

#include "net/HttpFetch.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>
#include <QTextDocument>
#include <QUdpSocket>
#include <QUrlQuery>
#include <QWidget>

#include <algorithm>

QString launchHtmlFragmentToPlainText(const QString &html, int maxLength = 260)
{
    QTextDocument document;
    document.setHtml(QString("<html><body>%1</body></html>").arg(html));
    return compactPlainText(document.toPlainText(), maxLength);
}

QJsonArray parseDarkSpaceDevelopmentLog(const QByteArray &bytes, const QString &logUrl)
{
    QJsonArray items;
    const QString html = QString::fromLatin1(bytes);
    // The DarkSpace log page has no RSS feed. Parse the page structure used by
    // the current site and group each release table into one launcher entry.
    static const QRegularExpression versionExpression(
        R"(<table class='cp-toolbar'[^>]*>\s*<tr><td class='cp-toolbar__title'>((?:(?!</td>).)*)</td>(?:(?!</table>).)*</table>\s*<div class='cp-scroll'>((?:(?!</div>).)*)</div>)",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rowExpression(
        R"(<tr[^>]*>\s*<td[^>]*>.*?</td>\s*<td>(.*?)</td>\s*</tr>)",
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator versions = versionExpression.globalMatch(html);
    while (versions.hasNext() && items.size() < 12) {
        const QRegularExpressionMatch versionMatch = versions.next();
        const QString versionTitle = launchHtmlFragmentToPlainText(versionMatch.captured(1));
        if (versionTitle.compare("Development log", Qt::CaseInsensitive) == 0) {
            continue;
        }

        QStringList bodyParts;
        QRegularExpressionMatchIterator rows = rowExpression.globalMatch(versionMatch.captured(2));
        while (rows.hasNext()) {
            const QString rowBody = launchHtmlFragmentToPlainText(rows.next().captured(1), 2400);
            if (rowBody.isEmpty()) {
                continue;
            }
            bodyParts << rowBody;
        }

        if (!bodyParts.isEmpty()) {
            QJsonObject item;
            item.insert("title", versionTitle.isEmpty() ? QString("Development update") : versionTitle);
            item.insert("body", bodyParts.join("\n"));
            item.insert("url", logUrl);
            items.append(item);
        }
    }

    return items;
}

DarkSpaceUpdatesResult fetchDarkSpaceUpdates(const QUrl &url, int timeoutMs)
{
    DarkSpaceUpdatesResult result;
    QString error;
    const QByteArray bytes = fetchUrlBytes(nullptr, url, &error, timeoutMs);
    if (bytes.isEmpty()) {
        result.error = error.isEmpty() ? QString("The DarkSpace development log returned no data.") : error;
        return result;
    }

    result.items = parseDarkSpaceDevelopmentLog(bytes, url.toString());
    if (result.items.isEmpty()) {
        result.error = "The DarkSpace development log could not be parsed.";
    }
    return result;
}
QString comparablePath(QString path)
{
    path = QDir::cleanPath(QDir::fromNativeSeparators(path));
    return path.toLower();
}

QString vdfValue(const QString &text, const QString &key)
{
    const QRegularExpression pattern(QString("\"%1\"\\s+\"([^\"]*)\"")
                                         .arg(QRegularExpression::escape(key)),
                                     QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(text);
    return match.hasMatch() ? match.captured(1).replace("\\\"", "\"") : QString();
}

QString steamLibraryRootForExecutable(const QString &executable)
{
    const QString path = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(executable).absoluteFilePath()));
    const QString lower = path.toLower();
    constexpr auto marker = "/steamapps/common/";
    const int index = lower.indexOf(marker);
    return index >= 0 ? path.left(index) : QString();
}

SteamLocalApp detectSteamAppForExecutable(const QString &executable)
{
    const QFileInfo executableInfo(executable);
    if (!executableInfo.exists()) {
        return {};
    }

    const QString libraryRoot = steamLibraryRootForExecutable(executableInfo.absoluteFilePath());
    if (!libraryRoot.isEmpty()) {
        // Steam installs can be identified even when launched from GameCQ by
        // matching the executable path against appmanifest installdir entries.
        const QDir steamAppsDir(QDir(libraryRoot).filePath("steamapps"));
        const QStringList manifests = steamAppsDir.entryList({"appmanifest_*.acf"}, QDir::Files, QDir::Name);
        const QString executablePath = comparablePath(executableInfo.absoluteFilePath());

        for (const QString &manifestName : manifests) {
            QFile manifest(steamAppsDir.filePath(manifestName));
            if (!manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }

            const QString text = QString::fromUtf8(manifest.readAll());
            const QString installDir = vdfValue(text, "installdir");
            if (installDir.isEmpty()) {
                continue;
            }

            const QString installPath = comparablePath(QDir(steamAppsDir.filePath("common")).filePath(installDir));
            if (executablePath == installPath || executablePath.startsWith(installPath + "/")) {
                SteamLocalApp app;
                app.appId = vdfValue(text, "appid");
                app.name = vdfValue(text, "name");
                app.installDir = installDir;
                app.manifestPath = QDir::cleanPath(manifest.fileName());
                return app;
            }
        }
    }

    QDir dir = executableInfo.absoluteDir();
    for (int i = 0; i < 4; ++i) {
        // Some non-Steam-distributed games still ship steam_appid.txt for
        // overlay/server-browser integration.
        QFile appIdFile(dir.filePath("steam_appid.txt"));
        if (appIdFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            SteamLocalApp app;
            app.appId = QString::fromUtf8(appIdFile.readLine()).trimmed();
            app.name = executableInfo.completeBaseName();
            return app;
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    return {};
}

SteamRemoteMetadata fetchSteamRemoteMetadata(QWidget *parent, const QString &appId, QString *error)
{
    SteamRemoteMetadata metadata;
    metadata.appId = appId;
    if (appId.isEmpty()) {
        return metadata;
    }

    const QUrl storeUrl(QString("https://store.steampowered.com/api/appdetails?appids=%1&cc=us&l=en").arg(appId));
    const QByteArray storeBytes = fetchUrlBytes(parent, storeUrl, error);
    const QJsonDocument storeDoc = QJsonDocument::fromJson(storeBytes);
    const QJsonObject appObject = storeDoc.object().value(appId).toObject();
    if (appObject.value("success").toBool()) {
        const QJsonObject data = appObject.value("data").toObject();
        metadata.name = data.value("name").toString();
        metadata.shortDescription = data.value("short_description").toString();
        metadata.headerImageUrl = data.value("header_image").toString();
        metadata.capsuleImageUrl = data.value("capsule_image").toString();
        if (metadata.capsuleImageUrl.isEmpty()) {
            metadata.capsuleImageUrl = metadata.headerImageUrl;
        }
        metadata.backgroundImageUrl = data.value("background_raw").toString();
        if (metadata.backgroundImageUrl.isEmpty()) {
            metadata.backgroundImageUrl = data.value("background").toString();
        }
        metadata.screenshots = data.value("screenshots").toArray();
    }

    QString newsError;
    const QUrl newsUrl(QString("https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=%1&count=5&maxlength=500&format=json").arg(appId));
    const QByteArray newsBytes = fetchUrlBytes(parent, newsUrl, &newsError, 10000);
    const QJsonDocument newsDoc = QJsonDocument::fromJson(newsBytes);
    metadata.newsItems = newsDoc.object().value("appnews").toObject().value("newsitems").toArray();

    return metadata;
}

QString steamAppListCachePath()
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(".Cache/Steam/app-list.json"));
}

QList<SteamSearchResult> steamAppListFromBytes(const QByteArray &bytes)
{
    QList<SteamSearchResult> apps;
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    const QJsonArray appArray = document.object().value("applist").toObject().value("apps").toArray();
    apps.reserve(appArray.size());
    for (const QJsonValue &value : appArray) {
        const QJsonObject app = value.toObject();
        SteamSearchResult result;
        result.appId = QString::number(app.value("appid").toInteger());
        result.name = app.value("name").toString().trimmed();
        if (!result.appId.isEmpty() && !result.name.isEmpty()) {
            apps.append(result);
        }
    }
    return apps;
}

QList<SteamSearchResult> steamAppList(QWidget *parent, QString *error)
{
    if (error) {
        error->clear();
    }

    const QString cachePath = steamAppListCachePath();
    const QFileInfo cacheInfo(cachePath);
    const bool cacheFresh = cacheInfo.exists() && cacheInfo.lastModified().toUTC() > QDateTime::currentDateTimeUtc().addDays(-7);

    auto readCache = [&]() {
        QFile file(cachePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        return file.readAll();
    };

    QByteArray bytes;
    if (cacheFresh) {
        bytes = readCache();
    }

    if (bytes.isEmpty()) {
        if (cacheInfo.exists()) {
            bytes = readCache();
        }
    }

    return steamAppListFromBytes(bytes);
}

QList<SteamSearchResult> steamStoreSearch(QWidget *parent, const QString &query, QString *error)
{
    if (error) {
        error->clear();
    }

    QList<SteamSearchResult> matches;
    const QString trimmedQuery = query.trimmed();
    if (trimmedQuery.isEmpty()) {
        return matches;
    }

    QUrl url("https://store.steampowered.com/api/storesearch/");
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("term", trimmedQuery);
    urlQuery.addQueryItem("l", "en");
    urlQuery.addQueryItem("cc", "us");
    url.setQuery(urlQuery);

    QString networkError;
    const QByteArray bytes = fetchUrlBytes(parent, url, &networkError, 15000);
    if (bytes.isEmpty()) {
        if (error && !networkError.isEmpty()) {
            *error = QString("Steam Store search failed: %1").arg(networkError);
        }
        return matches;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QString("Steam Store search returned unreadable data: %1").arg(parseError.errorString());
        }
        return matches;
    }

    const QJsonArray items = document.object().value("items").toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        if (!item.value("type").toString().isEmpty() && item.value("type").toString() != "app") {
            continue;
        }

        SteamSearchResult result;
        result.appId = QString::number(item.value("id").toInteger());
        result.name = item.value("name").toString().trimmed();
        if (result.appId == "0" || result.name.isEmpty()) {
            continue;
        }

        bool duplicate = false;
        for (const SteamSearchResult &existing : matches) {
            if (existing.appId == result.appId) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            matches.append(result);
        }
    }

    return matches;
}

QString normalizedSteamSearchText(QString text)
{
    text = text.toLower();
    text.replace(QRegularExpression("[^a-z0-9]+"), " ");
    return text.simplified();
}

int steamSearchScore(const QString &query, const QString &candidate)
{
    const QString normalizedQuery = normalizedSteamSearchText(query);
    const QString normalizedCandidate = normalizedSteamSearchText(candidate);
    if (normalizedQuery.isEmpty() || normalizedCandidate.isEmpty()) {
        return 0;
    }
    if (normalizedCandidate == normalizedQuery) {
        return 1000;
    }

    QString compactQuery = normalizedQuery;
    compactQuery.remove(' ');
    QString compactCandidate = normalizedCandidate;
    compactCandidate.remove(' ');
    if (!compactQuery.isEmpty() && !compactCandidate.isEmpty()) {
        if (compactCandidate == compactQuery) {
            return 980;
        }
        if (compactCandidate.startsWith(compactQuery)) {
            return 820 - qMin(200, compactCandidate.size() - compactQuery.size());
        }
        if (compactCandidate.contains(compactQuery)) {
            return 700 - qMin(180, compactCandidate.size() - compactQuery.size());
        }
    }

    if (normalizedCandidate.startsWith(normalizedQuery)) {
        return 850 - qMin(200, normalizedCandidate.size() - normalizedQuery.size());
    }
    if (normalizedCandidate.contains(normalizedQuery)) {
        return 720 - qMin(180, normalizedCandidate.size() - normalizedQuery.size());
    }

    const QStringList tokens = normalizedQuery.split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return 0;
    }

    int hits = 0;
    int score = 0;
    for (const QString &token : tokens) {
        if (token.size() <= 1) {
            continue;
        }
        const int index = normalizedCandidate.indexOf(token);
        if (index >= 0) {
            ++hits;
            score += 120 - qMin(60, index);
        }
    }

    if (hits == tokens.size()) {
        score += 260;
    }
    return score;
}

QList<SteamSearchResult> steamSearchMatches(QWidget *parent, const QString &query, QString *error)
{
    struct ScoredResult {
        SteamSearchResult result;
        int score = 0;
    };

    QList<ScoredResult> scored;
    auto addScored = [&](const SteamSearchResult &result, int score) {
        if (result.appId.isEmpty() || result.name.isEmpty() || score <= 0) {
            return;
        }
        for (ScoredResult &existing : scored) {
            if (existing.result.appId == result.appId) {
                existing.score = qMax(existing.score, score);
                return;
            }
        }
        scored.append({result, score});
    };

    QString storeError;
    const QList<SteamSearchResult> storeMatches = steamStoreSearch(parent, query, &storeError);
    for (int i = 0; i < storeMatches.size(); ++i) {
        // Store search ranking is useful, but combine it with local scoring so
        // exact title matches from the app list can outrank loose suggestions.
        addScored(storeMatches.at(i), qMax(steamSearchScore(query, storeMatches.at(i).name), 900 - i));
    }

    const QList<SteamSearchResult> apps = steamAppList(parent, nullptr);
    for (const SteamSearchResult &app : apps) {
        addScored(app, steamSearchScore(query, app.name));
    }

    if (error) {
        *error = scored.isEmpty() ? storeError : QString();
    }

    std::stable_sort(scored.begin(), scored.end(), [](const ScoredResult &left, const ScoredResult &right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return QString::localeAwareCompare(left.result.name, right.result.name) < 0;
    });

    QList<SteamSearchResult> matches;
    for (const ScoredResult &match : scored) {
        matches.append(match.result);
        if (matches.size() >= 25) {
            break;
        }
    }
    return matches;
}

void removeLaunchArtworkVariants(const QString &folder, const QString &baseName)
{
    const QDir dir(folder);
    for (const QString &extension : launchArtworkExtensions()) {
        QFile::remove(dir.filePath(QString("%1.%2").arg(baseName, extension)));
    }
}
bool writeTextFile(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    file.write(text.toUtf8());
    return true;
}

bool downloadUrlToFile(QWidget *parent, const QUrl &url, const QString &path, QString *error)
{
    if (!url.isValid() || url.scheme().isEmpty()) {
        return false;
    }

    const QByteArray bytes = fetchUrlBytes(parent, url, error, 20000);
    if (bytes.isEmpty()) {
        return false;
    }

    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QString("Could not write %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }
    file.write(bytes);
    return true;
}

QString downloadedLaunchArtworkPath(QWidget *parent, const LaunchEntry &entry, const QString &urlText, const QString &kind)
{
    const QUrl url(urlText);
    if (!url.isValid() || url.scheme().isEmpty()) {
        return {};
    }

    QString extension = QFileInfo(url.path()).suffix().toLower();
    if (extension.isEmpty() || !launchArtworkExtensions().contains(extension)) {
        extension = "jpg";
    }

    const QString folder = launchArtworkFolder(entry, true);
    removeLaunchArtworkVariants(folder, kind);
    const QString destination = QDir(folder).filePath(QString("%1.%2").arg(kind, extension));
    QString error;
    if (!downloadUrlToFile(parent, url, destination, &error)) {
        return {};
    }
    return QDir::cleanPath(destination);
}

void writeSteamMetadataFiles(const LaunchEntry &entry, const SteamRemoteMetadata &metadata)
{
    const QString folder = launchArtworkFolder(entry, true);
    if (!metadata.appId.trimmed().isEmpty()) {
        QJsonObject root;
        root.insert("provider", "steam");
        root.insert("appId", metadata.appId.trimmed());
        root.insert("name", metadata.name.trimmed());
        root.insert("screenshots", metadata.screenshots);
        QFile file(QDir(folder).filePath("steam.json"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        }
    }
    if (!metadata.shortDescription.trimmed().isEmpty()) {
        writeTextFile(QDir(folder).filePath("description.txt"), metadata.shortDescription.trimmed());
    }
    if (!metadata.newsItems.isEmpty()) {
        QJsonObject root;
        root.insert("provider", "steam");
        root.insert("appId", metadata.appId);
        root.insert("items", metadata.newsItems);
        QFile file(QDir(folder).filePath("news.json"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        }
    }
}

QString steamAppIdFromJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    return root.value("appId").toString().trimmed();
}

QString launchSteamAppId(const LaunchEntry &entry)
{
    if (!entry.steamAppId.trimmed().isEmpty()) {
        return entry.steamAppId.trimmed();
    }

    const QString folder = launchArtworkFolder(entry);
    QString appId = steamAppIdFromJson(QDir(folder).filePath("steam.json"));
    if (!appId.isEmpty()) {
        return appId;
    }

    appId = steamAppIdFromJson(QDir(folder).filePath("news.json"));
    if (!appId.isEmpty()) {
        return appId;
    }

    const SteamLocalApp localApp = detectSteamAppForExecutable(resolveLaunchPath(entry, entry.executable));
    return localApp.appId.trimmed();
}

QString readSteamCString(const QByteArray &data, int &offset)
{
    const int end = data.indexOf('\0', offset);
    if (end < 0) {
        offset = data.size();
        return {};
    }
    const QString value = QString::fromUtf8(data.constData() + offset, end - offset);
    offset = end + 1;
    return value;
}

QByteArray steamInfoRequest(const QByteArray &challenge = {})
{
    QByteArray request;
    request.append(char(0xFF));
    request.append(char(0xFF));
    request.append(char(0xFF));
    request.append(char(0xFF));
    request.append('T');
    request.append("Source Engine Query");
    request.append('\0');
    request.append(challenge);
    return request;
}

bool enrichSteamServerInfo(ServerInfo &server, int timeoutMs = 180)
{
    QHostAddress address(server.address);
    if (address.isNull() || server.port <= 0 || server.port > 65535) {
        return false;
    }

    QUdpSocket socket;
    auto readReply = [&]() -> QByteArray {
        if (!socket.waitForReadyRead(timeoutMs)) {
            return {};
        }
        QByteArray data;
        data.resize(int(socket.pendingDatagramSize()));
        socket.readDatagram(data.data(), data.size());
        return data;
    };

    socket.writeDatagram(steamInfoRequest(), address, quint16(server.port));
    QByteArray response = readReply();
    if (response.size() >= 9 && quint8(response.at(4)) == 0x41) {
        // Some Source-query servers require a challenge before returning A2S_INFO.
        socket.writeDatagram(steamInfoRequest(response.mid(5, 4)), address, quint16(server.port));
        response = readReply();
    }

    if (response.size() < 7 || quint8(response.at(4)) != 0x49) {
        return false;
    }

    int offset = 5;
    ++offset; // protocol
    const QString name = readSteamCString(response, offset);
    const QString map = readSteamCString(response, offset);
    readSteamCString(response, offset); // folder
    const QString game = readSteamCString(response, offset);
    offset += 2; // app id
    if (offset + 1 < response.size()) {
        server.clients = quint8(response.at(offset));
        server.maxClients = quint8(response.at(offset + 1));
        if (server.maxClients > 0) {
            server.population = QString("%1 / %2").arg(server.clients).arg(server.maxClients);
        }
    }

    if (!name.trimmed().isEmpty()) {
        server.name = name.trimmed();
    }
    if (!map.trimmed().isEmpty()) {
        server.shortDescription = QString("Map: %1").arg(map.trimmed());
    }
    if (!game.trimmed().isEmpty()) {
        server.description = game.trimmed();
    }
    return true;
}

QList<ServerInfo> querySteamMasterServers(const QString &appId, QString *error, int maxServers)
{
    if (error) {
        error->clear();
    }

    bool ok = false;
    const quint32 numericAppId = appId.toUInt(&ok);
    if (!ok || numericAppId == 0) {
        if (error) {
            *error = "Steam AppID is missing or invalid.";
        }
        return {};
    }

    const QHostInfo hostInfo = QHostInfo::fromName("hl2master.steampowered.com");
    QHostAddress masterAddress;
    for (const QHostAddress &candidate : hostInfo.addresses()) {
        if (candidate.protocol() == QAbstractSocket::IPv4Protocol) {
            masterAddress = candidate;
            break;
        }
    }
    if (masterAddress.isNull()) {
        if (error) {
            *error = "Could not resolve the Steam master server.";
        }
        return {};
    }

    QUdpSocket socket;
    if (!socket.bind(QHostAddress::AnyIPv4, 0)) {
        if (error) {
            *error = "Could not open a UDP socket for the Steam server probe.";
        }
        return {};
    }

    QList<ServerInfo> servers;
    QStringList seenEndpoints;
    QString startAddress = "0.0.0.0:0";
    QString lastError;
    // The master server is paged by sending the last endpoint from the previous
    // response. Keep this bounded so a launcher probe cannot monopolize the UI.
    for (int page = 0; page < 4 && servers.size() < maxServers; ++page) {
        QByteArray request;
        request.append('1');
        request.append(char(0xFF));
        request.append(startAddress.toLatin1());
        request.append('\0');
        request.append("\\appid\\");
        request.append(appId.trimmed().toLatin1());
        request.append('\0');

        if (socket.writeDatagram(request, masterAddress, 27011) < 0) {
            lastError = socket.errorString();
            break;
        }
        if (!socket.waitForReadyRead(2500)) {
            lastError = socket.errorString().isEmpty() ? QString("Steam master server did not respond.") : socket.errorString();
            break;
        }

        bool pageHadServers = false;
        while (socket.hasPendingDatagrams() && servers.size() < maxServers) {
            QByteArray response;
            response.resize(int(socket.pendingDatagramSize()));
            socket.readDatagram(response.data(), response.size());
            if (response.size() < 8 || quint8(response.at(0)) != 0x66) {
                continue;
            }

            for (int offset = 2; offset + 5 < response.size() && servers.size() < maxServers; offset += 6) {
                const QString address = QString("%1.%2.%3.%4")
                                            .arg(quint8(response.at(offset)))
                                            .arg(quint8(response.at(offset + 1)))
                                            .arg(quint8(response.at(offset + 2)))
                                            .arg(quint8(response.at(offset + 3)));
                const quint16 port = quint16((quint16(quint8(response.at(offset + 4))) << 8) | quint8(response.at(offset + 5)));
                if (address == "0.0.0.0" && port == 0) {
                    page = 4;
                    break;
                }

                const QString endpoint = QString("%1:%2").arg(address).arg(port);
                startAddress = endpoint;
                if (seenEndpoints.contains(endpoint, Qt::CaseInsensitive)) {
                    continue;
                }
                seenEndpoints.append(endpoint);

                ServerInfo server;
                server.gameId = numericAppId;
                server.name = endpoint;
                server.shortDescription = "Steam server";
                server.address = address;
                server.port = port;
                server.type = 0;
                servers.append(server);
                pageHadServers = true;
            }
        }

        if (!pageHadServers) {
            break;
        }
    }

    const int detailCount = qMin(servers.size(), 16);
    for (int i = 0; i < detailCount; ++i) {
        enrichSteamServerInfo(servers[i]);
    }

    if (servers.isEmpty() && error && !lastError.isEmpty()) {
        *error = lastError;
    }
    return servers;
}


QJsonObject readLaunchJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString compactPlainText(QString text, int maxLength)
{
    text.replace(QChar(0x00A0), ' ');
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    QStringList lines;
    for (const QString &line : text.split('\n')) {
        const QString simplified = line.simplified();
        if (!simplified.isEmpty()) {
            lines << simplified;
        }
    }
    text = lines.join("  ");
    if (text.size() > maxLength) {
        text = text.left(qMax(0, maxLength - 3)).trimmed() + "...";
    }
    return text;
}

bool isLikelyEnglishSteamNews(const QJsonObject &item)
{
    const QString title = item.value("title").toString().trimmed();
    if (title.isEmpty()) {
        return false;
    }

    int asciiLetters = 0;
    int nonAsciiLetters = 0;
    for (const QChar ch : title) {
        const ushort code = ch.unicode();
        if ((code >= 0x0400 && code <= 0x052F)  // Cyrillic
            || (code >= 0x0600 && code <= 0x06FF) // Arabic
            || (code >= 0x3040 && code <= 0x30FF) // Hiragana/Katakana
            || (code >= 0x3400 && code <= 0x9FFF) // CJK
            || (code >= 0xAC00 && code <= 0xD7AF)) { // Hangul
            return false;
        }
        if (!ch.isLetter()) {
            continue;
        }
        if (code <= 0x007F) {
            ++asciiLetters;
        } else {
            ++nonAsciiLetters;
        }
    }

    return asciiLetters >= 3 || nonAsciiLetters == 0;
}
QString plainLaunchHtmlText(const QString &html, int maxLength)
{
    if (html.trimmed().isEmpty()) {
        return {};
    }

    QTextDocument document;
    document.setHtml(QString("<html><body>%1</body></html>").arg(html));
    QString text = document.toPlainText();
    text.replace(QChar(0x00A0), ' ');
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    QStringList lines;
    for (const QString &line : text.split('\n')) {
        const QString simplified = line.simplified();
        if (!simplified.isEmpty()) {
            lines << simplified;
        }
    }
    text = lines.join("  ");
    if (text.size() > maxLength) {
        text = text.left(qMax(0, maxLength - 3)).trimmed() + "...";
    }
    return text;
}








