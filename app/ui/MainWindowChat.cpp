#include "ui/MainWindow.h"

#include "net/MetaClientBridge.h"
#include "ui/ChatFormatting.h"
#include "ui/ChatHtmlRenderer.h"
#include "ui/ChatLogWriter.h"
#include "ui/ChatMediaCache.h"
#include "ui/ChatUiSupport.h"
#include "ui/DiscordChatMedia.h"
#include "ui/LegacyIcons.h"
#include "ui/ProfilePresentation.h"
#include "ui/SideDrawerItems.h"
#include "ui/ThemeManager.h"

#include <QAbstractItemView>
#include <QColorDialog>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPoint>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSize>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#ifdef GAMECQ_HAS_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>
#endif

using namespace gamecq::chat_ui;

namespace {

QLabel *makeSectionLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("sectionLabel");
    return label;
}

QFrame *makePanel(const QString &objectName)
{
    auto *panel = new QFrame;
    panel->setObjectName(objectName);
    panel->setFrameShape(QFrame::NoFrame);
    return panel;
}

#ifdef GAMECQ_HAS_WEBENGINE
class ChatWebEnginePage final : public QWebEnginePage {
public:
    explicit ChatWebEnginePage(QObject *parent = nullptr)
        : QWebEnginePage(parent)
    {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override
    {
        if (type == QWebEnginePage::NavigationTypeLinkClicked) {
            QDesktopServices::openUrl(url);
            return false;
        }
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
};
#endif

} // namespace
QWidget *MainWindow::createChatPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setObjectName("chatSplitter");

    auto *chatPanel = makePanel("chatPanel");
    auto *chatLayout = new QVBoxLayout(chatPanel);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(0);

#ifdef GAMECQ_HAS_WEBENGINE
    auto *chatWebLog = new QWebEngineView(chatPanel);
    chatWebLog->setObjectName("chatLog");
    chatWebLog->setPage(new ChatWebEnginePage(chatWebLog));
    chatWebLog->settings()->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    chatWebLog->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    chatWebLog->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    connect(chatWebLog, &QWebEngineView::loadStarted, this, [this]() {
        m_chatWebReady = false;
    });
    connect(chatWebLog, &QWebEngineView::loadFinished, this, [this, chatWebLog](bool ok) {
        m_chatWebReady = ok;
        if (!ok) {
            return;
        }
        chatWebLog->page()->runJavaScript(renderReplaceChatMessagesScript(m_chatHtmlLines));
    });
    m_chatWebLog = chatWebLog;
    chatLayout->addWidget(chatWebLog, 1);
#else
    m_chatLog = new QTextBrowser(chatPanel);
    m_chatLog->setObjectName("chatLog");
    m_chatLog->setOpenExternalLinks(true);
    chatLayout->addWidget(m_chatLog, 1);
#endif

    refreshChatLogTheme();
    appendSystemChatLine("Login to join Darkspace lobby chat.");

    auto *composer = new QFrame(chatPanel);
    composer->setObjectName("composer");
    auto *composerLayout = new QHBoxLayout(composer);
    composerLayout->setContentsMargins(12, 8, 12, 8);
    composerLayout->setSpacing(8);

    m_chatInput = new QLineEdit(composer);
    m_chatInput->setPlaceholderText("Type a message...   /me  /tell  /away  /?");
    connect(m_chatInput, &QLineEdit::returnPressed, this, &MainWindow::postLocalChatLine);
    composerLayout->addWidget(m_chatInput, 1);

    m_chatColorButton = new QPushButton("T", composer);
    m_chatColorButton->setObjectName("chatTextButton");
    m_chatColorButton->setToolTip("Outgoing text color");
    connect(m_chatColorButton, &QPushButton::clicked, this, &MainWindow::chooseOutgoingChatColor);
    composerLayout->addWidget(m_chatColorButton);
    updateChatColorButton();

    auto *sendButton = new QPushButton(composer);
    sendButton->setObjectName("sendButton");
    sendButton->setToolTip("Send message");
    sendButton->setIcon(makeSendIcon(ThemeManager::color("primary")));
    sendButton->setIconSize(QSize(18, 18));
    connect(sendButton, &QPushButton::clicked, this, &MainWindow::postLocalChatLine);
    composerLayout->addWidget(sendButton);

    chatLayout->addWidget(composer);
    splitter->addWidget(chatPanel);

    auto *membersPanel = makePanel("membersPanel");
    auto *membersLayout = new QVBoxLayout(membersPanel);
    membersLayout->setContentsMargins(0, 0, 0, 0);
    membersLayout->setSpacing(0);

    auto *membersHeader = new QFrame(membersPanel);
    membersHeader->setObjectName("membersHeader");
    auto *membersHeaderLayout = new QHBoxLayout(membersHeader);
    membersHeaderLayout->setContentsMargins(12, 8, 12, 8);
    membersHeaderLayout->addWidget(makeSectionLabel("Room Members"));
    membersHeaderLayout->addStretch(1);

    auto *membersActions = new QFrame(membersPanel);
    membersActions->setObjectName("membersActions");
    auto *membersActionsLayout = new QHBoxLayout(membersActions);
    membersActionsLayout->setContentsMargins(8, 5, 8, 6);
    membersActionsLayout->setSpacing(4);

    auto makeSideButton = [this](const QString &text) {
        auto *button = createToolButton(text, ThemeManager::color("success"));
        button->setObjectName("sidePanelButton");
        button->setIconSize(QSize(12, 12));
        button->setCheckable(true);
        return button;
    };

    m_roomsButton = makeSideButton("Rooms");
    connect(m_roomsButton, &QToolButton::clicked, this, [this](bool checked) {
        if (!checked && m_sideDrawerMode == "rooms") {
            hideSideDrawer();
            return;
        }
        showSideDrawer("rooms", "Rooms", "Loading rooms...");
        m_bridge->requestRooms();
    });
    membersActionsLayout->addWidget(m_roomsButton);

    m_friendsButton = makeSideButton("Friends");
    connect(m_friendsButton, &QToolButton::clicked, this, [this](bool checked) {
        if (!checked && m_sideDrawerMode == "friends") {
            hideSideDrawer();
            return;
        }
        showSideDrawer("friends", "Friends");
        populateSideDrawerFriends();
        requestFriendsRefresh();
    });
    membersActionsLayout->addWidget(m_friendsButton);

    m_fleetButton = makeSideButton("Fleet");
    connect(m_fleetButton, &QToolButton::clicked, this, [this](bool checked) {
        if (!checked && m_sideDrawerMode == "fleet") {
            hideSideDrawer();
            return;
        }
        showSideDrawer("fleet", "Fleet", "Loading fleet...");
        m_bridge->requestFleet();
    });
    membersActionsLayout->addWidget(m_fleetButton);

    m_staffButton = makeSideButton("Staff");
    connect(m_staffButton, &QToolButton::clicked, this, [this](bool checked) {
        if (!isStaffProfile()) {
            return;
        }
        if (!checked && m_sideDrawerMode == "staff") {
            hideSideDrawer();
            return;
        }
        showSideDrawer("staff", "Staff", "Loading staff...");
        m_bridge->requestStaff();
    });
    m_staffButton->setVisible(false);
    membersActionsLayout->addWidget(m_staffButton);
    membersActionsLayout->addStretch(1);

    m_membersCount = new QLabel("0");
    membersHeaderLayout->addWidget(m_membersCount);
    membersLayout->addWidget(membersHeader);

    m_membersList = new QListWidget(membersPanel);
    m_membersList->setObjectName("membersList");
    m_membersList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_membersList, &QListWidget::customContextMenuRequested, this, &MainWindow::showMemberContextMenu);
    connect(m_membersList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        setChatCommand(QString("/send @%1 ").arg(item->data(kMemberUserIdRole).toUInt()));
    });
    membersLayout->addWidget(m_membersList, 1);
    membersLayout->addWidget(membersActions);

    splitter->addWidget(membersPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({900, 232});

    layout->addWidget(splitter, 1);

    m_sideDrawer = new QFrame(page);
    m_sideDrawer->setObjectName("sideDrawer");
    m_sideDrawer->setMinimumSize(260, 180);
    m_sideDrawer->setMaximumSize(380, 340);
    m_sideDrawer->resize(320, 300);
    auto *sideDrawerLayout = new QVBoxLayout(m_sideDrawer);
    sideDrawerLayout->setContentsMargins(0, 0, 0, 0);
    sideDrawerLayout->setSpacing(0);

    auto *sideDrawerHeader = new QFrame(m_sideDrawer);
    sideDrawerHeader->setObjectName("sideDrawerHeader");
    auto *sideDrawerHeaderLayout = new QHBoxLayout(sideDrawerHeader);
    sideDrawerHeaderLayout->setContentsMargins(8, 5, 8, 5);
    sideDrawerHeaderLayout->setSpacing(6);
    m_sideDrawerTitle = makeSectionLabel("Rooms");
    sideDrawerHeaderLayout->addWidget(m_sideDrawerTitle);
    sideDrawerHeaderLayout->addStretch(1);

    auto makeHeaderActionButton = [sideDrawerHeader](const QString &text, const QIcon &icon, const QString &toolTip) {
        auto *button = new QToolButton(sideDrawerHeader);
        button->setObjectName("sidePanelButton");
        button->setText(text);
        button->setIcon(icon);
        button->setIconSize(QSize(12, 12));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setToolTip(toolTip);
        button->setAutoRaise(false);
        return button;
    };

    m_sideDrawerPrimaryButton = makeHeaderActionButton("Create", legacyIcon("room"), "Create room");
    connect(m_sideDrawerPrimaryButton, &QToolButton::clicked, this, [this]() {
        if (m_sideDrawerMode == "rooms") {
            hideSideDrawer();
            showCreateRoomDialog();
        } else if (m_sideDrawerMode == "friends") {
            hideSideDrawer();
            showFriendsDialog();
        } else if (m_sideDrawerMode == "fleet") {
            hideSideDrawer();
            if (m_tabBar) {
                m_tabBar->setCurrentIndex(0);
            }
            navigateBrowser(QUrl("https://www.darkspace.net/index.php?lang=en&module=clans.php"));
        }
    });
    sideDrawerHeaderLayout->addWidget(m_sideDrawerPrimaryButton);

    m_sideDrawerRefreshButton = makeHeaderActionButton("Refresh", legacyIcon("activity"), "Refresh list");
    connect(m_sideDrawerRefreshButton, &QToolButton::clicked, this, [this]() {
        if (m_sideDrawerMode == "rooms") {
            showSideDrawer("rooms", "Rooms", "Loading rooms...");
            m_bridge->requestRooms();
        } else if (m_sideDrawerMode == "friends") {
            populateSideDrawerFriends();
            requestFriendsRefresh();
        } else if (m_sideDrawerMode == "fleet") {
            showSideDrawer("fleet", "Fleet", "Loading fleet...");
            m_bridge->requestFleet();
        } else if (m_sideDrawerMode == "staff") {
            showSideDrawer("staff", "Staff", "Loading staff...");
            m_bridge->requestStaff();
        }
    });
    sideDrawerHeaderLayout->addWidget(m_sideDrawerRefreshButton);

    auto *closeSideDrawer = new QToolButton(sideDrawerHeader);
    closeSideDrawer->setObjectName("sidePanelButton");
    closeSideDrawer->setText("X");
    closeSideDrawer->setToolTip("Close list");
    closeSideDrawer->setAutoRaise(false);
    connect(closeSideDrawer, &QToolButton::clicked, this, &MainWindow::hideSideDrawer);
    sideDrawerHeaderLayout->addWidget(closeSideDrawer);
    sideDrawerLayout->addWidget(sideDrawerHeader);

    m_sideDrawerList = new QListWidget(m_sideDrawer);
    m_sideDrawerList->setObjectName("sideDrawerList");
    m_sideDrawerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sideDrawerList->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sideDrawerList->setTextElideMode(Qt::ElideNone);
    m_sideDrawerList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sideDrawerList, &QListWidget::itemActivated, this, &MainWindow::handleSideDrawerItem);
    connect(m_sideDrawerList, &QListWidget::itemDoubleClicked, this, &MainWindow::handleSideDrawerItem);
    connect(m_sideDrawerList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        QListWidgetItem *item = m_sideDrawerList ? m_sideDrawerList->itemAt(position) : nullptr;
        if (!item || item->data(kSideItemKindRole).toString() != "profile") {
            return;
        }
        m_sideDrawerList->setCurrentItem(item);
        showProfileContextMenu(m_sideDrawerList, position);
    });
    sideDrawerLayout->addWidget(m_sideDrawerList, 1);
    m_sideDrawer->setVisible(false);
    m_sideDrawer->raise();

    return page;
}



