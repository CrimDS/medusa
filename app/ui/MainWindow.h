#pragma once

#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QList>
#include <QMainWindow>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QStringList>

#include "net/MetaClientTypes.h"

class MetaClientBridge;
class HttpManifestUpdater;
class QAction;
class ChatLogWriter;
class ChatMediaCache;
class QActionGroup;
class QCloseEvent;
class QComboBox;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QNetworkAccessManager;
class QProcess;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QSystemTrayIcon;
class QTabBar;
class QTextBrowser;
class QToolButton;
class QUrl;
class QVBoxLayout;
class StatusLine;
struct UserDialogsCallbacks;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    void setActiveTab(int index);
    void postLocalChatLine();
    void showLoginDialog();
    void onLoginSucceeded(const QString &displayName, quint32 flags, quint32 sessionId);
    void onLoginFailed(int result, const QString &message);
    void onGameSelected(const QString &name, quint32 gameId);
    void onGameLinksChanged(const GameLinks &links);
    void onRoomJoined(quint32 roomId, const QString &name);
    void onProfileChanged(const QString &displayName, quint32 flags);
    void onRoomsChanged(const QList<ChatRoomInfo> &rooms);
    void onChatMessagesReceived(const QList<ChatMessage> &messages);
    void onRoomMembersChanged(const QList<RoomMember> &members);
    void onFriendsChanged(const QList<RoomMember> &friends);
    void onFleetChanged(const QList<RoomMember> &members);
    void onStaffChanged(const QList<RoomMember> &staff);
    void onServersChanged(const QList<ServerInfo> &servers);
    void showMemberContextMenu(const QPoint &position);
    void showLaunchContextMenu(const QPoint &position);
    void showChangeNameDialog();
    void showFindUserDialog();
    void showFriendsDialog();
    void showIgnoredUsersDialog();
    void showOptionsDialog();
    void showAboutDialog();
    void showChatCommandsDialog();

private:
    // These helper groups are implemented in feature-specific MainWindow*.cpp
    // files. Keep new behavior near the feature it belongs to, and leave this
    // header as the shared state/slot index for Qt's meta-object system.
    UserDialogsCallbacks userDialogsCallbacks();

    // Window shell, status, tray, login/session, and global wiring.
    void createMenus();
    void createShell();
    void createStatusBar();
    void wireBridge();
    void applySettings();
    void applyThemeAppearance();
    void toggleMaximized();
    void updateWindowChromeState();
    void saveWorkspaceSplitSizes();
    void restoreWorkspaceSplitSizes();
    void createTrayIcon();
    void rebuildTrayMenu();
    void showMainWindowFromTray();
    void exitApplication();
    void ensureInstallLayout();
    bool tryAutoLogin();
    void clearPendingLogin();
    void persistPendingLogin();
    void wireLaunchUpdaters();

    // Primary pages and shared top-level controls.
    QWidget *createChatPage();
    QWidget *createBrowserPage();
    QWidget *createLaunchPage();

    QFrame *createToolbar();
    QToolButton *createToolButton(const QString &text, const QColor &color);
    QIcon makeDiamondIcon(const QColor &color, bool active = false) const;

    // Launcher library, detail panel, server selection, updates, and launch flow.
    void populateLaunchList();
    QString defaultLaunchSelectionId() const;
    void rebuildLaunchGroupFilter(const QString &preferredGroup = {});
    void saveLaunchLibraryControls() const;
    void updateLaunchDragMode();
    void handleLaunchListReordered();
    void createLaunchGroup();
    QString launchStatusText(const QString &launchId) const;
    bool launchEntrySharesCurrentUpdate(const QString &launchId) const;
    void refreshLaunchStatus();
    void updateLaunchDetails();
    void populateLaunchServerList(const QString &launchId);
    void updateLaunchServerCardExpansion();
    void populateDarkSpaceUpdatesPanel(const QString &launchId);
    void requestDarkSpaceUpdates(bool force = false);
    void rebuildLaunchActionsMenu();
    QString selectedLaunchId() const;
    void runSelectedLaunchEntry();
    void runRelatedLaunchEntry(const QString &kind);
    void openSelectedLaunchArtworkFolder();
    void chooseD12InstallFolder();
    void chooseLaunchArtwork(const QString &kind);
    void showAddLaunchEntryDialog();
    void showEditLaunchEntryDialog();
    void probeSelectedLaunchServers();
    void startHttpManifestUpdate(const QString &launchId, bool launchAfterUpdate = false, const ServerInfo *server = nullptr);
    void startLegacyMirrorUpdate(const QString &launchId, bool launchAfterUpdate = false, const ServerInfo *server = nullptr);
    void cancelLaunchUpdate();
    void clearPendingLaunch();
    void updateSelectedLaunchEntry();
    void deleteSelectedLaunchEntry();
    void requestFriendsRefresh();
    void populateFriendsMenu();
    void refreshServers();
    void selectServerForLaunch(const QString &launchId);
    void runSelectedLaunchEntryWithoutUpdateCheck();
    bool startLaunchEntry(const QString &launchId, const ServerInfo *server = nullptr, bool updateAlreadyChecked = false);
    QString preferredLaunchIdForServer(const ServerInfo &server) const;
    void rebuildServerFilterMenu();
    void setServerFilter(quint32 gameId, quint32 type);

    // Profile access, browser navigation, and user profile actions.
    bool isStaffProfile() const;
    bool isModeratorProfile() const;
    bool canBypassLaunchUpdates() const;
    void applyProfileAccess();
    void clearProfile();
    bool ensureBrowserView();
    void navigateBrowser(const QUrl &url, bool openExternalFallback = true);
    void openProfile(quint32 userId, const QString &name);

    // Chat composer, side drawers, and chat rendering/logging.
    void setChatCommand(const QString &command);
    void chooseOutgoingChatColor();
    void updateChatColorButton();
    void showSideDrawer(const QString &mode, const QString &title, const QString &loadingText = {});
    void hideSideDrawer();
    void setSideDrawerButtonsChecked(const QString &mode);
    void updateSideDrawerActions();
    void populateSideDrawerRooms(const QList<ChatRoomInfo> &rooms);
    void populateSideDrawerProfiles(const QString &mode, const QList<RoomMember> &members, const QString &emptyText);
    void populateSideDrawerFriends();
    void handleSideDrawerItem(QListWidgetItem *item);
    void showProfileContextMenu(QListWidget *list, const QPoint &position);
    void writeChatLogLine(const QString &line, quint32 seconds = 0);
    void refreshChatLogTheme();
    void loadRecentChatHistory();
    void appendChatHtmlLine(const QString &line);
    QString discordMediaPresentation(QString *text);
    void requestChatMedia(const QUrl &source, const QUrl &resource);
    void appendSystemChatLine(const QString &message);
    void appendChatMessage(const ChatMessage &message);
    void showCreateRoomDialog();

    QTabBar *m_tabBar = nullptr;
    QSplitter *m_lobbySplitter = nullptr;
    QStackedWidget *m_pages = nullptr;
    QTextBrowser *m_chatLog = nullptr;
    QWidget *m_chatWebLog = nullptr;
    QLineEdit *m_chatInput = nullptr;
    QPushButton *m_chatColorButton = nullptr;
    QWidget *m_browserView = nullptr;
    QWidget *m_browserMain = nullptr;
    QVBoxLayout *m_browserMainLayout = nullptr;
    QLineEdit *m_browserAddress = nullptr;
    QAction *m_browserBackAction = nullptr;
    QAction *m_browserForwardAction = nullptr;
    QAction *m_browserStopAction = nullptr;
    QFrame *m_launchDetailPanel = nullptr;
    QListWidget *m_launchList = nullptr;
    QComboBox *m_launchKindFilter = nullptr;
    QComboBox *m_launchGroupFilter = nullptr;
    QComboBox *m_launchSort = nullptr;
    QLabel *m_launchArt = nullptr;
    QFrame *m_launchServersPanel = nullptr;
    QLabel *m_launchTitle = nullptr;
    QLabel *m_launchSubtitle = nullptr;
    QLabel *m_launchStatusPill = nullptr;
    QLabel *m_launchDescription = nullptr;
    QLabel *m_launchInstallPath = nullptr;
    QLabel *m_launchServersTitle = nullptr;
    QListWidget *m_launchServersList = nullptr;
    QLabel *m_launchServersStatus = nullptr;
    QPushButton *m_launchServersRefreshButton = nullptr;
    QFrame *m_launchUpdatesPanel = nullptr;
    QLabel *m_launchUpdatesStatus = nullptr;
    QListWidget *m_launchUpdatesList = nullptr;
    QPushButton *m_launchPlayButton = nullptr;
    QPushButton *m_launchRepairButton = nullptr;
    QPushButton *m_launchSetupButton = nullptr;
    QPushButton *m_launchTutorialButton = nullptr;
    QPushButton *m_launchMoreButton = nullptr;
    QToolButton *m_maximizeButton = nullptr;
    QToolButton *m_serverFilterButton = nullptr;
    QToolButton *m_roomsButton = nullptr;
    QToolButton *m_friendsButton = nullptr;
    QToolButton *m_fleetButton = nullptr;
    QToolButton *m_staffButton = nullptr;
    QListWidget *m_membersList = nullptr;
    QLabel *m_membersCount = nullptr;
    QFrame *m_sideDrawer = nullptr;
    QLabel *m_sideDrawerTitle = nullptr;
    QToolButton *m_sideDrawerPrimaryButton = nullptr;
    QToolButton *m_sideDrawerRefreshButton = nullptr;
    QListWidget *m_sideDrawerList = nullptr;
    QMenu *m_roomsMenu = nullptr;
    QMenu *m_friendsMenu = nullptr;
    QMenu *m_fleetMenu = nullptr;
    QMenu *m_staffMenu = nullptr;
    QMenu *m_launchActionsMenu = nullptr;
    QMenu *m_serverFilterMenu = nullptr;
    QMenu *m_trayMenu = nullptr;
    QActionGroup *m_serverFilterGroup = nullptr;
    QAction *m_launchCancelAction = nullptr;
    QAction *m_hideAction = nullptr;
    QAction *m_unhideAction = nullptr;
    StatusLine *m_statusLine = nullptr;
    MetaClientBridge *m_bridge = nullptr;
    HttpManifestUpdater *m_d12Updater = nullptr;
    QProcess *m_legacyMirrorProcess = nullptr;
    ChatLogWriter *m_chatLogWriter = nullptr;
    ChatMediaCache *m_chatMediaCache = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QString m_launchUpdateId;
    QString m_launchUpdateStatus;
    QString m_launchUpdateDetail;
    QStringList m_chatHtmlLines;
    QJsonArray m_darkSpaceUpdates;
    QString m_darkSpaceUpdatesError;
    bool m_chatWebReady = false;
    bool m_darkSpaceUpdatesLoading = false;
    QString m_serverLaunchId;
    QString m_pendingLaunchId;
    ServerInfo m_pendingLaunchServer;
    QHash<QString, QList<ServerInfo>> m_launchDiscoveredServers;
    QSet<QString> m_launchServerProbeIds;
    bool m_pendingLaunchHasServer = false;
    bool m_pendingLaunchAfterUpdate = false;
    bool m_legacyMirrorCancelRequested = false;
    int m_legacyMirrorProgressPercent = -1;
    QString m_chatColor;
    QString m_sideDrawerMode;
    QString m_pendingLoginAddress;
    QString m_pendingLoginAccount;
    QString m_pendingLoginPassword;
    int m_pendingLoginPort = 0;
    bool m_pendingRememberName = false;
    bool m_pendingRememberPassword = false;
    bool m_pendingAutoLogin = false;
    bool m_autoLoginAttempted = false;
    bool m_autoLoginInProgress = false;
    QString m_profileName;
    QString m_browserCurrentUrl;
    QString m_browserPendingUrl;
    GameLinks m_gameLinks;
    QList<ServerInfo> m_servers;
    QList<RoomMember> m_friends;
    QList<int> m_workspaceSplitSizes;
    bool m_friendsLoaded = false;
    bool m_friendsLoading = false;
    bool m_adjustingLobbySplitter = false;
    bool m_updatingLaunchList = false;
    bool m_exitRequested = false;
    quint32 m_sessionId = 0;
    quint32 m_profileFlags = 0;
    quint32 m_serverGameFilter = 0;
    quint32 m_serverTypeFilter = 0x03;
};

