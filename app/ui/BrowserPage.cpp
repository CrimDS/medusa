#include "ui/BrowserPage.h"

#include "ui/BrowserSupport.h"
#include "ui/ThemeManager.h"

#include <QAction>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#ifdef GAMECQ_HAS_WEBENGINE
#include <QWebEngineHistory>
#include <QWebEngineView>
#endif

namespace {

QFrame *makeBrowserPanel(const QString &objectName, QWidget *parent = nullptr)
{
    auto *frame = new QFrame(parent);
    frame->setObjectName(objectName);
    frame->setFrameShape(QFrame::NoFrame);
    return frame;
}

QFrame *createBrowserToolbar(QWidget *parent)
{
    auto *toolbar = makeBrowserPanel("contextToolbar", parent);
    auto *layout = new QHBoxLayout(toolbar);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);
    return toolbar;
}

QToolButton *createBrowserButton(const QString &text, const QColor &color, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setIcon(QIcon());
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setAutoRaise(false);
    button->setProperty("diamondColor", color.name(QColor::HexRgb));
    return button;
}

void navigate(const BrowserPageCallbacks &callbacks, const QUrl &url, bool externalFallback = true)
{
    if (callbacks.navigate) {
        callbacks.navigate(url, externalFallback);
    }
}

QWidget *browserView(const BrowserPageCallbacks &callbacks)
{
    return callbacks.browserView ? callbacks.browserView() : nullptr;
}

bool ensureBrowserView(const BrowserPageCallbacks &callbacks)
{
    return callbacks.ensureBrowserView ? callbacks.ensureBrowserView() : false;
}

} // namespace

BrowserPageParts createBrowserPageWidget(QWidget *parent, const BrowserPageCallbacks &callbacks)
{
#ifdef GAMECQ_HAS_WEBENGINE
    configureWebEngineProfile();
#endif

    BrowserPageParts parts;
    parts.page = new QWidget(parent);
    auto *layout = new QVBoxLayout(parts.page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *toolbar = createBrowserToolbar(parts.page);
    auto *toolbarLayout = qobject_cast<QHBoxLayout *>(toolbar->layout());

    auto *destinationsMenu = new QMenu(toolbar);
    destinationsMenu->addAction("Home", toolbar, [callbacks]() {
        if (callbacks.homeUrl) {
            navigate(callbacks, callbacks.homeUrl());
        }
    });
    destinationsMenu->addAction("News", toolbar, [callbacks]() {
        if (callbacks.newsUrl) {
            navigate(callbacks, callbacks.newsUrl());
        }
    });
    destinationsMenu->addAction("Forum", toolbar, [callbacks]() {
        if (callbacks.forumUrl) {
            navigate(callbacks, callbacks.forumUrl());
        }
    });
    destinationsMenu->addAction("Downloads", toolbar, [callbacks]() {
        if (callbacks.downloadsUrl) {
            navigate(callbacks, callbacks.downloadsUrl());
        }
    });
    destinationsMenu->addAction("Profile", toolbar, [callbacks]() {
        if (callbacks.openCurrentProfile) {
            callbacks.openCurrentProfile();
        }
    });

    auto *destinations = createBrowserButton("DS", ThemeManager::color("blue"), toolbar);
    destinations->setObjectName("browserDestinationButton");
    destinations->setToolTip("DarkSpace pages");
    destinations->setMenu(destinationsMenu);
    destinations->setPopupMode(QToolButton::InstantPopup);
    toolbarLayout->addWidget(destinations);

    auto *browserMenu = new QMenu(toolbar);
    QAction *back = browserMenu->addAction("Back");
    QAction *forward = browserMenu->addAction("Forward");
    QAction *reload = browserMenu->addAction("Reload");
    QAction *stop = browserMenu->addAction("Stop");
    browserMenu->addSeparator();
    QAction *openExternal = browserMenu->addAction("Open in Default Browser");
    browserMenu->addSeparator();
    QAction *zoomIn = browserMenu->addAction("Zoom In");
    QAction *zoomOut = browserMenu->addAction("Zoom Out");
    QAction *actualSize = browserMenu->addAction("Actual Size");

    auto *browserControls = createBrowserButton("...", ThemeManager::color("textMuted"), toolbar);
    browserControls->setObjectName("browserMenuButton");
    browserControls->setToolTip("Browser controls and options");
    browserControls->setMenu(browserMenu);
    browserControls->setPopupMode(QToolButton::InstantPopup);
    toolbarLayout->addWidget(browserControls);

    auto *addressFrame = new QFrame(toolbar);
    addressFrame->setObjectName("browserAddressFrame");
    auto *addressLayout = new QHBoxLayout(addressFrame);
    addressLayout->setContentsMargins(0, 0, 4, 0);
    addressLayout->setSpacing(0);

    auto *address = new QLineEdit(addressFrame);
    address->setObjectName("browserAddress");
    address->setPlaceholderText("Enter an address");
    address->setClearButtonEnabled(false);
    address->setMinimumWidth(180);
    addressLayout->addWidget(address, 1);

    auto *clearAddress = new QToolButton(addressFrame);
    clearAddress->setObjectName("browserAddressClear");
    clearAddress->setText("X");
    clearAddress->setToolTip("Clear address");
    clearAddress->setFixedSize(18, 18);
    clearAddress->setVisible(false);
    addressLayout->addWidget(clearAddress, 0, Qt::AlignVCenter);
    QObject::connect(clearAddress, &QToolButton::clicked, address, [address]() {
        address->clear();
        address->setFocus();
    });
    QObject::connect(address, &QLineEdit::textChanged, clearAddress, [clearAddress](const QString &text) {
        clearAddress->setVisible(!text.isEmpty());
    });
    QObject::connect(address, &QLineEdit::returnPressed, toolbar, [callbacks, address]() {
        const QUrl target = QUrl::fromUserInput(address->text().trimmed());
        if (target.isValid()) {
            navigate(callbacks, target);
        }
    });
    toolbarLayout->addWidget(addressFrame, 1);

    parts.address = address;
    parts.backAction = back;
    parts.forwardAction = forward;
    parts.stopAction = stop;

    QObject::connect(openExternal, &QAction::triggered, toolbar, [callbacks]() {
        const QString currentText = callbacks.currentUrl ? callbacks.currentUrl() : QString();
        const QUrl current = browserDisplayUrl(QUrl::fromUserInput(currentText));
        const QUrl fallback = callbacks.homeUrl ? callbacks.homeUrl() : QUrl(darkSpaceHomeUrlString());
        QDesktopServices::openUrl(current.isValid() ? current : fallback);
    });
#ifdef GAMECQ_HAS_WEBENGINE
    QObject::connect(back, &QAction::triggered, toolbar, [callbacks]() {
        if (ensureBrowserView(callbacks)) {
            if (auto *view = qobject_cast<QWebEngineView *>(browserView(callbacks))) {
                view->back();
            }
        }
    });
    QObject::connect(forward, &QAction::triggered, toolbar, [callbacks]() {
        if (ensureBrowserView(callbacks)) {
            if (auto *view = qobject_cast<QWebEngineView *>(browserView(callbacks))) {
                view->forward();
            }
        }
    });
    QObject::connect(reload, &QAction::triggered, toolbar, [callbacks]() {
        if (ensureBrowserView(callbacks)) {
            if (auto *view = qobject_cast<QWebEngineView *>(browserView(callbacks))) {
                view->reload();
            }
        }
    });
    QObject::connect(stop, &QAction::triggered, toolbar, [callbacks]() {
        if (auto *view = qobject_cast<QWebEngineView *>(browserView(callbacks))) {
            view->stop();
        }
    });
    QObject::connect(zoomIn, &QAction::triggered, toolbar, [callbacks]() {
        if (ensureBrowserView(callbacks)) {
            if (auto *view = qobject_cast<QWebEngineView *>(browserView(callbacks))) {
                view->setZoomFactor(qMin(3.0, view->zoomFactor() + 0.1));
            }
        }
    });
    QObject::connect(zoomOut, &QAction::triggered, toolbar, [callbacks]() {
        if (ensureBrowserView(callbacks)) {
            if (auto *view = qobject_cast<QWebEngineView *>(browserView(callbacks))) {
                view->setZoomFactor(qMax(0.25, view->zoomFactor() - 0.1));
            }
        }
    });
    QObject::connect(actualSize, &QAction::triggered, toolbar, [callbacks]() {
        if (ensureBrowserView(callbacks)) {
            if (auto *view = qobject_cast<QWebEngineView *>(browserView(callbacks))) {
                view->setZoomFactor(1.0);
            }
        }
    });
#else
    back->setEnabled(false);
    forward->setEnabled(false);
    stop->setEnabled(false);
    zoomIn->setEnabled(false);
    zoomOut->setEnabled(false);
    actualSize->setEnabled(false);
    QObject::connect(reload, &QAction::triggered, toolbar, [callbacks]() {
        const QString currentText = callbacks.currentUrl ? callbacks.currentUrl() : QString();
        navigate(callbacks, QUrl::fromUserInput(currentText.isEmpty() ? darkSpaceHomeUrlString() : currentText), false);
    });
#endif
    layout->addWidget(toolbar);

    auto *body = makeBrowserPanel("browserBody", parts.page);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    parts.main = makeBrowserPanel("browserMain", body);
    parts.mainLayout = new QVBoxLayout(parts.main);
    parts.mainLayout->setContentsMargins(0, 0, 0, 0);
    parts.mainLayout->setSpacing(0);
#ifdef GAMECQ_HAS_WEBENGINE
    QObject::connect(browserMenu, &QMenu::aboutToShow, toolbar, [callbacks, back, forward]() {
        if (auto *view = qobject_cast<QWebEngineView *>(browserView(callbacks))) {
            back->setEnabled(view->history()->canGoBack());
            forward->setEnabled(view->history()->canGoForward());
            return;
        }
        back->setEnabled(false);
        forward->setEnabled(false);
    });
    stop->setEnabled(false);

    auto *placeholder = new QTextBrowser(parts.main);
    placeholder->setObjectName("browserFallback");
    placeholder->setOpenExternalLinks(true);
    placeholder->setHtml(browserStartHtml());
    parts.view = placeholder;
    parts.mainLayout->addWidget(placeholder, 1);
    QTimer::singleShot(400, parts.page, [callbacks]() {
        ensureBrowserView(callbacks);
    });
#else
    auto *fallback = new QTextBrowser(parts.main);
    fallback->setObjectName("browserFallback");
    fallback->setOpenExternalLinks(true);
    fallback->setHtml(browserStartHtml());
    parts.view = fallback;
    parts.mainLayout->addWidget(fallback, 1);
#endif
    bodyLayout->addWidget(parts.main, 1);

    layout->addWidget(body, 1);
    return parts;
}