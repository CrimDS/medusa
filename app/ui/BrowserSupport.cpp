#include "ui/BrowserSupport.h"

#include "core/AppPaths.h"
#include "ui/ThemeManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QUrlQuery>

#ifdef GAMECQ_HAS_WEBENGINE
#include <QWebEngineProfile>
#endif

namespace {

QString browserPanelHtml(const QString &title, const QString &body, const QString &link = {})
{
    const QString pageBg = ThemeManager::colorName("pageBg");
    const QString panelBg = ThemeManager::colorName("panelBg");
    const QString text = ThemeManager::colorName("text");
    const QString textStrong = ThemeManager::colorName("textStrong");
    const QString textMuted = ThemeManager::colorName("textMuted");
    const QString border = ThemeManager::colorName("border");
    const QString primary = ThemeManager::colorName("primary");
    const QString blue = ThemeManager::colorName("blue");
    const QString font = ThemeManager::fontFamily().toHtmlEscaped();
    const QString escapedLink = link.toHtmlEscaped();
    const QString linkHtml = escapedLink.isEmpty()
        ? QString()
        : QString("<div style=\"color:%1;margin-bottom:8px;\">Current page</div><a style=\"color:%2;\" href=\"%3\">%3</a>").arg(textMuted, blue, escapedLink);

    return QString(
               "<html><body style=\"background:%1;color:%2;font-family:%3,Tahoma,sans-serif;margin:0;padding:22px;\">"
               "<div style=\"color:%4;font-weight:700;border-bottom:1px solid %5;padding-bottom:10px;margin-bottom:16px;\">DarkSpace Web</div>"
               "<div style=\"background:%6;border:1px solid %5;border-radius:8px;padding:18px;max-width:720px;\">"
               "<div style=\"font-size:20px;font-weight:800;color:%7;margin-bottom:8px;\">%8</div>"
               "<div style=\"color:%9;line-height:1.45;margin-bottom:14px;\">%10</div>"
               "%11"
               "</div>"
               "</body></html>")
        .arg(pageBg,
             text,
             font,
             primary,
             border,
             panelBg,
             textStrong,
             title.toHtmlEscaped(),
             textMuted,
             body.toHtmlEscaped(),
             linkHtml);
}

QString browserSettingString(const QString &key, const QString &fallback = {})
{
    return QSettings().value(key, fallback).toString();
}

constexpr auto kDarkSpaceHomeUrl = "https://www.darkspace.net/";
constexpr auto kDarkSpaceDownloadsUrl = "https://www.darkspace.net/downloads/";
constexpr auto kDarkSpaceManualUrl = "https://www.darkspace.net/wiki/index.php?title=Manual";
constexpr auto kDarkSpaceNewsUrl = "https://www.darkspace.net/news/";
constexpr auto kDarkSpaceForumUrl = "https://www.darkspace.net/forum/";

} // namespace

QString browserFallbackHtml(const QUrl &url)
{
    return browserPanelHtml(
        "Embedded browser unavailable",
        "This Qt build does not include WebEngine. Browser actions will open live pages in your default browser.",
        url.toString());
}

QString browserLoadTroubleHtml(const QUrl &url)
{
    return browserPanelHtml(
        "Page is taking too long to load",
        "The embedded browser stopped waiting so GameCQ stays responsive. You can try Reload from the browser menu or open the page externally.",
        url.toString());
}

QString browserStartHtml()
{
    return browserPanelHtml(
        "Browser ready",
        "GameCQ will load the community site after lobby login so the embedded browser can reuse your session.");
}

bool isDarkSpaceWebUrl(const QUrl &url)
{
    const QString host = url.host().toLower();
    return host == "darkspace.net" || host.endsWith(".darkspace.net");
}

bool isBrowserStartupUrl(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    return scheme.isEmpty() || scheme == "about" || scheme == "data" || scheme == "qrc";
}

QUrl browserDisplayUrl(QUrl url)
{
    QUrlQuery query(url);
    query.removeQueryItem("sid");
    url.setQuery(query);
    return url;
}

bool isDarkSpaceHomeLikeUrl(const QUrl &url)
{
    if (!isDarkSpaceWebUrl(url)) {
        return false;
    }

    QString path = url.path().toLower();
    if (path.isEmpty() || path == "/") {
        return true;
    }
    if (path.endsWith('/')) {
        path.chop(1);
    }
    return path == "/index" || path == "/index.htm" || path == "/index.html" || path == "/index.php";
}

void configureWebEngineProfile()
{
#ifdef GAMECQ_HAS_WEBENGINE
    static bool configured = false;
    if (configured) {
        return;
    }

    auto *profile = QWebEngineProfile::defaultProfile();
    if (!profile) {
        return;
    }

    const QString root = AppPaths::localDataRoot("GameCQWeb", QDir(QCoreApplication::applicationDirPath()).filePath(".Cache/GameCQWeb"));

    QDir dir(root);
    dir.mkpath("WebCache");
    dir.mkpath("WebStorage");
    profile->setCachePath(dir.filePath("WebCache"));
    profile->setPersistentStoragePath(dir.filePath("WebStorage"));
    profile->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
    profile->setHttpCacheMaximumSize(256 * 1024 * 1024);
    configured = true;
#endif
}

QUrl browserUrlWithSession(const QUrl &url, quint32 sessionId)
{
    if (sessionId == 0 || !isDarkSpaceWebUrl(url)) {
        return url;
    }

    QUrl sessionUrl = url;
    QUrlQuery query(sessionUrl);
    query.removeQueryItem("sid");
    query.addQueryItem("sid", QString::number(sessionId));
    sessionUrl.setQuery(query);
    return sessionUrl;
}

QUrl browserUrlForSession(const QString &configuredUrl, const QString &fallbackUrl, quint32 sessionId)
{
    const QString trimmed = configuredUrl.trimmed();
    QUrl url = trimmed.isEmpty() ? QUrl(fallbackUrl) : QUrl::fromUserInput(trimmed);
    if (url.isRelative()) {
        url = QUrl(kDarkSpaceHomeUrl).resolved(url);
    }
    return browserUrlWithSession(url.isValid() ? url : QUrl(fallbackUrl), sessionId);
}

QUrl browserHomeUrl(const GameLinks &links, quint32 sessionId)
{
    const QString overrideUrl = browserSettingString("Browser/HomeUrl").trimmed();
    if (!overrideUrl.isEmpty()) {
        return browserUrlForSession(overrideUrl, kDarkSpaceHomeUrl, sessionId);
    }

    return browserUrlForSession(links.home, kDarkSpaceHomeUrl, sessionId);
}

QUrl browserProfileUrl(const GameLinks &links, quint32 sessionId, quint32 userId)
{
    QUrl url = browserUrlForSession(links.profile, kDarkSpaceHomeUrl, sessionId);
    QUrlQuery query(url);
    if (sessionId != 0) {
        query.removeQueryItem("sid");
        query.addQueryItem("sid", QString::number(sessionId));
    }
    if (userId != 0) {
        query.removeQueryItem("view");
        query.addQueryItem("view", QString::number(userId));
    }
    url.setQuery(query);
    return url;
}

bool shouldRefreshBrowserForSession(bool hasBrowserView, const QString &currentUrl, quint32 sessionId)
{
    if (!hasBrowserView || sessionId == 0) {
        return false;
    }

    if (currentUrl.isEmpty()) {
        return true;
    }

    const QUrl current = QUrl::fromUserInput(currentUrl);
    if (isBrowserStartupUrl(current)) {
        return true;
    }

    if (isDarkSpaceWebUrl(current)) {
        const QUrlQuery query(current);
        return query.queryItemValue("sid") != QString::number(sessionId);
    }

    return false;
}

bool isBrowserAtStartupOrHome(const QString &currentUrl)
{
    if (currentUrl.isEmpty()) {
        return true;
    }

    const QUrl current = QUrl::fromUserInput(currentUrl);
    return isBrowserStartupUrl(current) || isDarkSpaceHomeLikeUrl(current);
}

QString darkSpaceHomeUrlString()
{
    return QString::fromLatin1(kDarkSpaceHomeUrl);
}

QString darkSpaceNewsUrlString()
{
    return QString::fromLatin1(kDarkSpaceNewsUrl);
}

QString darkSpaceForumUrlString()
{
    return QString::fromLatin1(kDarkSpaceForumUrl);
}

QString darkSpaceDownloadsUrlString()
{
    return QString::fromLatin1(kDarkSpaceDownloadsUrl);
}

QString darkSpaceManualUrlString()
{
    return QString::fromLatin1(kDarkSpaceManualUrl);
}
