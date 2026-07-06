#pragma once

#include <QColor>
#include <QString>
#include <QUrl>

#include <functional>

class QAction;
class QLineEdit;
class QVBoxLayout;
class QWidget;

struct BrowserPageParts {
    QWidget *page = nullptr;
    QWidget *main = nullptr;
    QVBoxLayout *mainLayout = nullptr;
    QWidget *view = nullptr;
    QLineEdit *address = nullptr;
    QAction *backAction = nullptr;
    QAction *forwardAction = nullptr;
    QAction *stopAction = nullptr;
};

struct BrowserPageCallbacks {
    std::function<void(const QUrl &url, bool externalFallback)> navigate;
    std::function<void()> openCurrentProfile;
    std::function<QUrl()> homeUrl;
    std::function<QUrl()> newsUrl;
    std::function<QUrl()> forumUrl;
    std::function<QUrl()> downloadsUrl;
    std::function<QString()> currentUrl;
    std::function<bool()> ensureBrowserView;
    std::function<QWidget *()> browserView;
};

BrowserPageParts createBrowserPageWidget(QWidget *parent, const BrowserPageCallbacks &callbacks);