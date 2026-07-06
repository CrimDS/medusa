#include "ui/MainWindow.h"

#include "ui/BrowserPage.h"
#include "ui/BrowserSupport.h"
#include "ui/ThemeManager.h"

#ifdef GAMECQ_HAS_WEBENGINE
#include <QWebEngineView>
#endif

#include <QAction>
#include <QDesktopServices>
#include <QLineEdit>
#include <QStatusBar>
#include <QTabBar>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

bool MainWindow::ensureBrowserView()
{
#ifdef GAMECQ_HAS_WEBENGINE
    if (qobject_cast<QWebEngineView *>(m_browserView)) {
        return true;
    }

    if (!m_browserMain || !m_browserMainLayout) {
        return false;
    }

    configureWebEngineProfile();

    QWidget *oldView = m_browserView;
    if (oldView) {
        m_browserMainLayout->removeWidget(oldView);
        oldView->deleteLater();
    }

    auto *webView = new QWebEngineView(m_browserMain);
    webView->setObjectName("browserWebView");
    webView->page()->setBackgroundColor(ThemeManager::color("pageBg"));
    m_browserView = webView;
    connect(webView, &QWebEngineView::urlChanged, this, [this](const QUrl &url) {
        m_browserCurrentUrl = url.toString();
        if (m_browserPendingUrl == m_browserCurrentUrl) {
            m_browserPendingUrl.clear();
        }
        if (m_browserAddress) {
            m_browserAddress->setText(isBrowserStartupUrl(url) ? QString() : browserDisplayUrl(url).toString());
        }
    });
    connect(webView, &QWebEngineView::loadStarted, this, [this]() {
        if (m_browserStopAction) {
            m_browserStopAction->setEnabled(true);
        }
        statusBar()->showMessage("Loading DarkSpace web...");
    });
    connect(webView, &QWebEngineView::loadStarted, webView, [this, webView]() {
        QTimer::singleShot(25000, webView, [this, webView]() {
            if (!m_browserStopAction || !m_browserStopAction->isEnabled() || m_browserPendingUrl.isEmpty()) {
                return;
            }
            const QUrl timedOutUrl = QUrl::fromUserInput(m_browserPendingUrl);
            m_browserPendingUrl.clear();
            webView->stop();
            webView->setHtml(browserLoadTroubleHtml(timedOutUrl), timedOutUrl);
            statusBar()->showMessage("DarkSpace web timed out in the embedded browser.", 7000);
        });
    });
    connect(webView, &QWebEngineView::loadProgress, this, [this](int progress) {
        if (progress > 0 && progress < 100) {
            statusBar()->showMessage(QString("Loading DarkSpace web... %1%").arg(progress));
        }
    });
    connect(webView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_browserPendingUrl.clear();
        if (m_browserStopAction) {
            m_browserStopAction->setEnabled(false);
        }
        statusBar()->showMessage(ok ? QString("DarkSpace web ready.") : QString("DarkSpace web load failed."), 3000);
    });

    m_browserMainLayout->addWidget(webView, 1);
    webView->setHtml(browserStartHtml(), QUrl(darkSpaceHomeUrlString()));
    return true;
#else
    return false;
#endif
}

void MainWindow::navigateBrowser(const QUrl &url, bool openExternalFallback)
{
    const QUrl target = browserUrlWithSession(url, m_sessionId);
    if (!target.isValid()) {
        return;
    }

    const QString targetText = target.toString();
    if (targetText == m_browserCurrentUrl || targetText == m_browserPendingUrl) {
        return;
    }
    m_browserCurrentUrl = targetText;

#ifdef GAMECQ_HAS_WEBENGINE
    ensureBrowserView();
    if (auto *view = qobject_cast<QWebEngineView *>(m_browserView)) {
        m_browserPendingUrl = targetText;
        view->setUrl(target);
        return;
    }
#else
    if (auto *fallback = qobject_cast<QTextBrowser *>(m_browserView)) {
        fallback->setHtml(browserFallbackHtml(target));
    }
    if (openExternalFallback) {
        QDesktopServices::openUrl(target);
        statusBar()->showMessage(QString("Opened %1 in your browser.").arg(target.toString()), 3000);
    }
#endif
}

void MainWindow::openProfile(quint32 userId, const QString &name)
{
    if (m_tabBar) {
        m_tabBar->setCurrentIndex(0);
    }
    navigateBrowser(browserProfileUrl(m_gameLinks, m_sessionId, userId));
    if (!name.isEmpty()) {
        statusBar()->showMessage(QString("Opening profile for %1.").arg(name), 3000);
    }
}

QWidget *MainWindow::createBrowserPage()
{
    BrowserPageCallbacks callbacks;
    callbacks.navigate = [this](const QUrl &url, bool externalFallback) {
        navigateBrowser(url, externalFallback);
    };
    callbacks.openCurrentProfile = [this]() {
        openProfile(0, m_profileName);
    };
    callbacks.homeUrl = [this]() {
        return browserHomeUrl(m_gameLinks, m_sessionId);
    };
    callbacks.newsUrl = [this]() {
        return browserUrlForSession(m_gameLinks.news, darkSpaceNewsUrlString(), m_sessionId);
    };
    callbacks.forumUrl = [this]() {
        return browserUrlForSession(m_gameLinks.forum, darkSpaceForumUrlString(), m_sessionId);
    };
    callbacks.downloadsUrl = [this]() {
        return browserUrlForSession(m_gameLinks.download, darkSpaceDownloadsUrlString(), m_sessionId);
    };
    callbacks.currentUrl = [this]() {
        return m_browserCurrentUrl;
    };
    callbacks.ensureBrowserView = [this]() {
        return ensureBrowserView();
    };
    callbacks.browserView = [this]() {
        return m_browserView;
    };

    const BrowserPageParts parts = createBrowserPageWidget(this, callbacks);
    m_browserMain = parts.main;
    m_browserMainLayout = parts.mainLayout;
    m_browserView = parts.view;
    m_browserAddress = parts.address;
    m_browserBackAction = parts.backAction;
    m_browserForwardAction = parts.forwardAction;
    m_browserStopAction = parts.stopAction;
    return parts.page;
}

