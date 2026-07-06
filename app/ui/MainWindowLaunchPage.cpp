#include "ui/MainWindow.h"

#include "launcher/LaunchCatalog.h"
#include "launcher/LaunchEntryDialog.h"
#include "launcher/HttpManifestUpdater.h"
#include "launcher/LaunchMetadata.h"
#include "launcher/LaunchRunner.h"
#include "net/GameProtocolConstants.h"
#include "net/ProfileFlags.h"
#include "ui/BrowserSupport.h"
#include "ui/ChatLogWriter.h"
#include "ui/LaunchInfoList.h"
#include "ui/LaunchPresentation.h"
#include "ui/LaunchWidgets.h"
#include "ui/LegacyIcons.h"
#include "ui/StatusLine.h"
#include "ui/ThemeManager.h"

#ifdef GAMECQ_HAS_WEBENGINE
#include <QWebEngineView>
#endif

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QProcess>
#include <QPushButton>
#include <QRadialGradient>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QStatusBar>
#include <QSplitter>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent>

#include <algorithm>

#include "ui/MainWindowLaunchSupport.h"

using namespace gamecq::launch_ui;


QWidget *MainWindow::createLaunchPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *scrollArea = new QScrollArea(page);
    scrollArea->setObjectName("launchScrollArea");
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto *body = makePanel("pageBody");
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(12, 12, 12, 12);
    bodyLayout->setSpacing(12);

    auto *launchSplitter = new QSplitter(Qt::Horizontal, body);
    launchSplitter->setObjectName("launchSplitter");
    launchSplitter->setChildrenCollapsible(false);

    auto *library = makePanel("launchLibraryPanel");
    library->setMinimumWidth(230);
    library->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *libraryLayout = new QVBoxLayout(library);
    libraryLayout->setContentsMargins(12, 12, 12, 12);
    libraryLayout->setSpacing(10);

    auto *libraryHeader = new QHBoxLayout;
    libraryHeader->setContentsMargins(0, 0, 0, 0);
    libraryHeader->setSpacing(8);
    libraryHeader->addWidget(makeSectionLabel("Library"));
    libraryHeader->addStretch(1);
    auto *addLaunch = new QPushButton("Add");
    addLaunch->setObjectName("launchSmallButton");
    connect(addLaunch, &QPushButton::clicked, this, &MainWindow::showAddLaunchEntryDialog);
    libraryHeader->addWidget(addLaunch);
    libraryLayout->addLayout(libraryHeader);

    auto *libraryControls = makePanel("launchFilterBar");
    auto *libraryControlsLayout = new QHBoxLayout(libraryControls);
    libraryControlsLayout->setContentsMargins(6, 6, 6, 6);
    libraryControlsLayout->setSpacing(5);

    m_launchKindFilter = new QComboBox(libraryControls);
    m_launchKindFilter->setObjectName("launchFilterCombo");
    m_launchKindFilter->setToolTip("Filter the library by entry type.");
    m_launchKindFilter->setMinimumHeight(26);
    m_launchKindFilter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_launchKindFilter->addItem("All", "all");
    m_launchKindFilter->addItem("Games", "game");
    m_launchKindFilter->addItem("Apps", "software");
    const QString savedKindFilter = settingString("Launch/LibraryKindFilter", "all").toLower();
    const int savedKindIndex = m_launchKindFilter->findData(savedKindFilter);
    m_launchKindFilter->setCurrentIndex(savedKindIndex >= 0 ? savedKindIndex : 0);

    m_launchGroupFilter = new QComboBox(libraryControls);
    m_launchGroupFilter->setObjectName("launchFilterCombo");
    m_launchGroupFilter->setToolTip("Filter the library by group, or create a new group.");
    m_launchGroupFilter->setMinimumHeight(26);
    m_launchGroupFilter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_launchSort = new QComboBox(libraryControls);
    m_launchSort->setObjectName("launchFilterCombo");
    m_launchSort->setToolTip("Choose how the library is ordered. Manual order enables drag and drop.");
    m_launchSort->setMinimumHeight(26);
    m_launchSort->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_launchSort->setIconSize(QSize(14, 14));
    const QIcon sortIcon = legacyIcon("sort");
    m_launchSort->addItem(sortIcon, "Manual", "custom");
    m_launchSort->addItem(sortIcon, "Name", "name");
    m_launchSort->addItem(sortIcon, "Recent", "lastUsed");
    m_launchSort->addItem(sortIcon, "Newest", "newest");
    m_launchSort->addItem(sortIcon, "Group", "group");
    const QString savedSort = settingString("Launch/LibrarySort", "custom");
    const int savedSortIndex = m_launchSort->findData(savedSort);
    m_launchSort->setCurrentIndex(savedSortIndex >= 0 ? savedSortIndex : 0);

    libraryControlsLayout->addWidget(m_launchKindFilter);
    libraryControlsLayout->addWidget(m_launchGroupFilter);
    libraryControlsLayout->addWidget(m_launchSort);
    libraryLayout->addWidget(libraryControls);

    m_launchList = new QListWidget(library);
    m_launchList->setObjectName("launchList");
    m_launchList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_launchList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_launchList->setIconSize(QSize(24, 24));
    m_launchList->setUniformItemSizes(true);
    m_launchList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_launchList->setDragDropMode(QAbstractItemView::InternalMove);
    m_launchList->setDefaultDropAction(Qt::MoveAction);
    m_launchList->setDropIndicatorShown(true);
    m_launchList->setDragEnabled(true);
    m_launchList->setAcceptDrops(true);
    connect(m_launchList, &QListWidget::customContextMenuRequested, this, &MainWindow::showLaunchContextMenu);
    connect(m_launchList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        runSelectedLaunchEntry();
    });
    connect(m_launchList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        updateLaunchDetails();
    });
    connect(m_launchList->model(), &QAbstractItemModel::rowsMoved, this, [this]() {
        handleLaunchListReordered();
    });
    libraryLayout->addWidget(m_launchList, 1);

    rebuildLaunchGroupFilter(settingString("Launch/LibraryGroupFilter", "all"));
    connect(m_launchKindFilter, &QComboBox::currentIndexChanged, this, [this](int) {
        saveLaunchLibraryControls();
        populateLaunchList();
    });
    connect(m_launchGroupFilter, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updatingLaunchList) {
            return;
        }
        const QString value = m_launchGroupFilter->currentData().toString();
        if (value == "__new__") {
            createLaunchGroup();
            return;
        }
        saveLaunchLibraryControls();
        populateLaunchList();
    });
    connect(m_launchSort, &QComboBox::currentIndexChanged, this, [this](int) {
        saveLaunchLibraryControls();
        updateLaunchDragMode();
        populateLaunchList();
    });
    updateLaunchDragMode();

    m_launchDetailPanel = new LaunchFeatureFrame;
    auto *detail = m_launchDetailPanel;
    detail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(12, 12, 12, 12);
    detailLayout->setSpacing(10);

    auto *infoOverlay = makePanel("launchInfoOverlay");
    infoOverlay->setMinimumHeight(116);
    infoOverlay->setMaximumHeight(156);
    infoOverlay->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *infoLayout = new QHBoxLayout(infoOverlay);
    infoLayout->setContentsMargins(12, 12, 12, 12);
    infoLayout->setSpacing(14);

    m_launchArt = new LaunchArtLabel(infoOverlay);
    m_launchArt->setObjectName("launchCapsule");
    m_launchArt->setAlignment(Qt::AlignCenter);
    m_launchArt->setFixedSize(180, 102);
    m_launchArt->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_launchArt->setWordWrap(true);
    infoLayout->addWidget(m_launchArt, 0);

    auto *infoStack = new QVBoxLayout;
    infoStack->setContentsMargins(0, 0, 0, 0);
    infoStack->setSpacing(8);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(10);
    auto *titleStack = new QVBoxLayout;
    titleStack->setContentsMargins(0, 0, 0, 0);
    titleStack->setSpacing(2);
    m_launchTitle = new QLabel(detail);
    m_launchTitle->setObjectName("launchTitle");
    m_launchTitle->setWordWrap(true);
    m_launchSubtitle = new QLabel(detail);
    m_launchSubtitle->setObjectName("launchSubtitle");
    m_launchSubtitle->setWordWrap(true);
    titleStack->addWidget(m_launchTitle);
    titleStack->addWidget(m_launchSubtitle);
    titleRow->addLayout(titleStack, 1);
    m_launchStatusPill = new QLabel(detail);
    m_launchStatusPill->setAlignment(Qt::AlignCenter);
    titleRow->addWidget(m_launchStatusPill, 0, Qt::AlignTop);
    infoStack->addLayout(titleRow);

    m_launchDescription = new QLabel(detail);
    m_launchDescription->setObjectName("launchDescription");
    m_launchDescription->setWordWrap(true);
    infoStack->addWidget(m_launchDescription);

    m_launchInstallPath = new QLabel(detail);
    m_launchInstallPath->setObjectName("launchPath");
    m_launchInstallPath->setWordWrap(true);
    m_launchInstallPath->setVisible(false);
    infoStack->addWidget(m_launchInstallPath);
    infoStack->addStretch(1);
    infoLayout->addLayout(infoStack, 1);
    detailLayout->addWidget(infoOverlay, 0);

    auto *actionsOverlay = makePanel("launchActionsOverlay");
    auto *actionsLayout = new QVBoxLayout(actionsOverlay);
    actionsLayout->setContentsMargins(12, 10, 12, 10);
    actionsLayout->setSpacing(0);
    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->setSpacing(8);
    m_launchPlayButton = new QPushButton("Play", detail);
    m_launchPlayButton->setObjectName("playButton");
    connect(m_launchPlayButton, &QPushButton::clicked, this, &MainWindow::runSelectedLaunchEntry);
    actionRow->addWidget(m_launchPlayButton);

    m_launchRepairButton = new QPushButton("Install / Repair", detail);
    m_launchRepairButton->setObjectName("launchSecondaryButton");
    connect(m_launchRepairButton, &QPushButton::clicked, this, &MainWindow::updateSelectedLaunchEntry);
    actionRow->addWidget(m_launchRepairButton);

    m_launchSetupButton = new QPushButton("Setup", detail);
    m_launchSetupButton->setObjectName("launchSecondaryButton");
    connect(m_launchSetupButton, &QPushButton::clicked, this, [this]() {
        runRelatedLaunchEntry("setup");
    });
    actionRow->addWidget(m_launchSetupButton);

    m_launchTutorialButton = new QPushButton("Tutorial", detail);
    m_launchTutorialButton->setObjectName("launchSecondaryButton");
    connect(m_launchTutorialButton, &QPushButton::clicked, this, [this]() {
        runRelatedLaunchEntry("tutorial");
    });
    actionRow->addWidget(m_launchTutorialButton);

    m_launchActionsMenu = new QMenu(detail);
    connect(m_launchActionsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildLaunchActionsMenu);
    m_launchMoreButton = new QPushButton("Manage", detail);
    m_launchMoreButton->setObjectName("launchSecondaryButton");
    m_launchMoreButton->setMenu(m_launchActionsMenu);
    actionRow->addWidget(m_launchMoreButton);
    actionRow->addStretch(1);
    actionsLayout->addLayout(actionRow);
    detailLayout->addWidget(actionsOverlay, 0);

    auto *serversOverlay = makePanel("launchServersOverlay");
    m_launchServersPanel = serversOverlay;
    auto *serversLayout = new QVBoxLayout(serversOverlay);
    serversLayout->setContentsMargins(12, 10, 12, 12);
    serversLayout->setSpacing(8);
    auto *serversHeader = new QHBoxLayout;
    serversHeader->setContentsMargins(0, 0, 0, 0);
    serversHeader->setSpacing(8);
    m_launchServersTitle = makeSectionLabel("Servers");
    serversHeader->addWidget(m_launchServersTitle);
    serversHeader->addStretch(1);
    m_launchServersStatus = new QLabel(serversOverlay);
    m_launchServersStatus->setObjectName("launchServersStatus");
    serversHeader->addWidget(m_launchServersStatus);
    m_launchServersRefreshButton = new QPushButton("Refresh", serversOverlay);
    m_launchServersRefreshButton->setObjectName("launchSmallButton");
    connect(m_launchServersRefreshButton, &QPushButton::clicked, this, [this]() {
        const LaunchEntry entry = findLaunchEntry(selectedLaunchId());
        if (hasLaunchEntry(entry) && !entry.needsServer && !launchSteamAppId(entry).isEmpty()) {
            probeSelectedLaunchServers();
            return;
        }
        refreshServers();
    });
    serversHeader->addWidget(m_launchServersRefreshButton);
    serversLayout->addLayout(serversHeader);

    m_launchServersList = new QListWidget(detail);
    m_launchServersList->setObjectName("launchServersList");
    m_launchServersList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_launchServersList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_launchServersList->setIconSize(QSize(18, 18));
    m_launchServersList->setMinimumHeight(150);
    m_launchServersList->setSpacing(6);
    m_launchServersList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_launchServersList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_launchServersList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        updateLaunchServerCardExpansion();
    });
    connect(m_launchServersList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QUrl articleUrl = item ? item->data(kLaunchInfoUrlRole).toUrl() : QUrl();
        if (articleUrl.isValid()) {
            const LaunchEntry entry = findLaunchEntry(selectedLaunchId());
            if (isPrimaryDarkSpaceLaunchEntry(entry) && m_tabBar) {
                m_tabBar->setCurrentIndex(0);
                navigateBrowser(articleUrl);
                statusBar()->showMessage("Opening article in Browser.", 3000);
            } else {
                QDesktopServices::openUrl(articleUrl);
                statusBar()->showMessage("Opening article in your browser.", 3000);
            }
            return;
        }

        const QString launchId = selectedLaunchId();
        const LaunchEntry entry = findLaunchEntry(launchId);
        if (!item || !hasLaunchEntry(entry) || !entry.needsServer) {
            return;
        }

        if (hasLaunchServerEndpoint(item)) {
            const ServerInfo server = launchServerInfoFromItem(item);
            startLaunchEntry(launchId, &server);
        }
    });
    serversLayout->addWidget(m_launchServersList, 1);

    auto *updatesOverlay = makePanel("launchServersOverlay");
    m_launchUpdatesPanel = updatesOverlay;
    auto *updatesLayout = new QVBoxLayout(updatesOverlay);
    updatesLayout->setContentsMargins(12, 10, 12, 12);
    updatesLayout->setSpacing(8);
    auto *updatesHeader = new QHBoxLayout;
    updatesHeader->setContentsMargins(0, 0, 0, 0);
    updatesHeader->setSpacing(8);
    updatesHeader->addWidget(makeSectionLabel("Updates"));
    updatesHeader->addStretch(1);
    m_launchUpdatesStatus = new QLabel(updatesOverlay);
    m_launchUpdatesStatus->setObjectName("launchServersStatus");
    updatesHeader->addWidget(m_launchUpdatesStatus);
    auto *updatesRefresh = new QPushButton("Refresh", updatesOverlay);
    updatesRefresh->setObjectName("launchSmallButton");
    connect(updatesRefresh, &QPushButton::clicked, this, [this]() {
        requestDarkSpaceUpdates(true);
    });
    updatesHeader->addWidget(updatesRefresh);
    updatesLayout->addLayout(updatesHeader);

    m_launchUpdatesList = new QListWidget(detail);
    m_launchUpdatesList->setObjectName("launchServersList");
    m_launchUpdatesList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_launchUpdatesList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_launchUpdatesList->setMinimumHeight(150);
    m_launchUpdatesList->setSpacing(6);
    m_launchUpdatesList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_launchUpdatesList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_launchUpdatesList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *, QListWidgetItem *) {
        updateLaunchInfoCardExpansion(m_launchUpdatesList);
    });
    connect(m_launchUpdatesList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const QUrl articleUrl = item ? item->data(kLaunchInfoUrlRole).toUrl() : QUrl();
        if (articleUrl.isValid()) {
            if (m_tabBar) {
                m_tabBar->setCurrentIndex(0);
            }
            navigateBrowser(articleUrl);
            statusBar()->showMessage("Opening DarkSpace development log in Browser.", 3000);
        }
    });
    updatesLayout->addWidget(m_launchUpdatesList, 1);

    auto *lowerPanels = new QHBoxLayout;
    lowerPanels->setContentsMargins(0, 0, 0, 0);
    lowerPanels->setSpacing(10);
    lowerPanels->addWidget(serversOverlay, 1);
    lowerPanels->addWidget(updatesOverlay, 1);
    detailLayout->addLayout(lowerPanels, 1);

    launchSplitter->addWidget(library);
    launchSplitter->addWidget(detail);
    launchSplitter->setStretchFactor(0, 0);
    launchSplitter->setStretchFactor(1, 1);
    launchSplitter->setSizes({300, 1100});
    bodyLayout->addWidget(launchSplitter, 1);

    populateLaunchList();

    scrollArea->setWidget(body);
    layout->addWidget(scrollArea, 1);
    return page;
}

