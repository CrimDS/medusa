#include "ui/MainWindow.h"

#include "net/GameProtocolConstants.h"
#include "net/MetaClientBridge.h"
#include "net/ProfileFlags.h"
#include "ui/ChatLogWriter.h"
#include "ui/LegacyIcons.h"
#include "ui/StatusLine.h"

#include <QAction>
#include <QActionGroup>
#include <QIcon>
#include <QMenu>
#include <QPushButton>
#include <QStatusBar>
#include <QToolButton>

namespace {

QString serverFilterLabel(quint32 gameId, quint32 type)
{
    if (type == kGameServerType && gameId == 0) {
        return "Game Servers";
    }
    if (type == kGameServerType && gameId == kDarkSpaceGameId) {
        return "DarkSpace";
    }
    if (type == kGameServerType && gameId == kDarkSpaceBetaGameId) {
        return "Beta";
    }

    switch (type) {
    case kProfilerServerType:
        return "Profiler";
    case kProcessServerType:
        return "Process";
    case kMirrorServerType:
        return "Mirrors";
    case kMetaServerType:
        return "Meta";
    case kGameSubServerType:
        return "Subservers";
    case 0:
        return "Everything";
    default:
        return "Servers";
    }
}

bool isPublicServerFilter(quint32 gameId, quint32 type)
{
    return type == kGameServerType && (gameId == 0 || gameId == kDarkSpaceGameId);
}

QIcon legacyToolIconForText(const QString &text)
{
    const QString key = text.toLower();
    if (key.contains("message")) {
        return legacyIcon("friend");
    }
    if (key.contains("friend")) {
        return legacyIcon("friends");
    }
    if (key.contains("fleet") || key.contains("clan")) {
        return legacyIcon("clans");
    }
    if (key.contains("room")) {
        return legacyIcon("rooms");
    }
    if (key.contains("profile") || key.contains("user") || key.contains("staff") || key.contains("find") || key.contains("clone")) {
        return legacyIcon("Avatar");
    }
    if (key.contains("connect")) {
        return legacyIcon("game");
    }
    if (key.contains("launch") || key.contains("play") || key.contains("game") || key.contains("darkspace") || key.contains("server") || key.contains("open")) {
        return legacyIcon("games");
    }
    if (key.contains("emote")) {
        return legacyIcon("activity");
    }
    if (key.contains("create") || key.contains("add")) {
        return legacyIcon("game");
    }
    if (key.contains("install") || key.contains("download")) {
        return legacyIcon("down");
    }
    if (key.contains("repair") || key.contains("option") || key.contains("filter") || key.contains("process") || key.contains("profiler") || key.contains("watch") || key.contains("check") || key.contains("default") || key.contains("manage")) {
        return legacyIcon("tool");
    }
    if (key.contains("delete") || key.contains("remove")) {
        return legacyIcon("ico00002");
    }
    if (key.contains("cancel") || key.contains("ignore") || key.contains("close") || key.contains("mute") || key.contains("kick") || key.contains("ban")) {
        return legacyIcon("cancel");
    }
    if (key.contains("refresh")) {
        return legacyIcon("activity");
    }
    if (key.contains("forward")) {
        return legacyIcon("right");
    }
    if (key.contains("back")) {
        return legacyIcon("up");
    }
    if (key.contains("home") || key.contains("news") || key.contains("forum") || key.contains("manual") || key.contains("command") || key.contains("folder")) {
        return legacyIcon("GCQLDoc");
    }
    return {};
}

QIcon legacyServerIcon(quint32 gameId, quint32 type)
{
    if (type == kMetaServerType) {
        return legacyIcon("GCQL");
    }
    if (type == kMirrorServerType) {
        return legacyIcon("MirrorUpload");
    }
    if (type == kProcessServerType) {
        return legacyIcon("ProcessClient");
    }
    if (type == kProfilerServerType) {
        return legacyIcon("tool");
    }
    if (type == kGameSubServerType) {
        return legacyIcon("game1");
    }
    if (gameId == kDarkSpaceBetaGameId) {
        return legacyIcon("game2");
    }
    if (type == kGameServerType) {
        return legacyIcon("game");
    }
    return legacyIcon("games");
}

} // namespace

void MainWindow::setServerFilter(quint32 gameId, quint32 type)
{
    if (!isStaffProfile() && !isPublicServerFilter(gameId, type)) {
        gameId = 0;
        type = kGameServerType;
    }

    m_serverGameFilter = gameId;
    m_serverTypeFilter = type;
    if (m_serverFilterButton) {
        const QString label = serverFilterLabel(m_serverGameFilter, m_serverTypeFilter);
        m_serverFilterButton->setText(label);
        const QIcon icon = legacyToolIconForText(label);
        if (!icon.isNull()) {
            m_serverFilterButton->setIcon(icon);
        }
    }
    rebuildServerFilterMenu();
    refreshServers();
}

bool MainWindow::isStaffProfile() const
{
    return (m_profileFlags & kStaffFlags) != 0;
}

bool MainWindow::isModeratorProfile() const
{
    return (m_profileFlags & kModeratorFlags) != 0;
}

bool MainWindow::canBypassLaunchUpdates() const
{
    return (m_profileFlags & (kAdministratorFlag | kDeveloperFlag)) != 0;
}

void MainWindow::applyProfileAccess()
{
    const bool canUseStaffTools = isStaffProfile();
    const bool canUseHide = (m_profileFlags & (kAdministratorFlag | kEventFlag)) != 0;
    if (m_hideAction) {
        m_hideAction->setVisible(canUseHide);
        m_hideAction->setEnabled(canUseHide && (m_profileFlags & kHiddenFlag) == 0);
    }
    if (m_unhideAction) {
        m_unhideAction->setVisible(canUseHide);
        m_unhideAction->setEnabled(canUseHide && (m_profileFlags & kHiddenFlag) != 0);
    }
    if (m_staffButton) {
        m_staffButton->setVisible(canUseStaffTools);
    }
    if (!canUseStaffTools && m_sideDrawerMode == "staff") {
        hideSideDrawer();
    }
    if (!canUseStaffTools && m_staffMenu) {
        m_staffMenu->clear();
    }
    populateLaunchList();
    if (!canUseStaffTools && !isPublicServerFilter(m_serverGameFilter, m_serverTypeFilter)) {
        m_serverGameFilter = 0;
        m_serverTypeFilter = kGameServerType;
    }
    if (m_serverFilterButton) {
        m_serverFilterButton->setText(serverFilterLabel(m_serverGameFilter, m_serverTypeFilter));
    }
    rebuildServerFilterMenu();
}

void MainWindow::clearProfile()
{
    m_profileName.clear();
    m_gameLinks = {};
    m_profileFlags = 0;
    m_sessionId = 0;
    m_friends.clear();
    m_friendsLoaded = false;
    m_friendsLoading = false;
    m_chatLogWriter->setAccountState(m_profileName, m_sessionId);
    if (m_statusLine) {
        m_statusLine->setProfile({}, 0);
    }
    applyProfileAccess();
}

void MainWindow::refreshServers()
{
    if (m_launchServersStatus) {
        m_launchServersStatus->setText("Refreshing...");
    }
    if (m_launchServersRefreshButton) {
        m_launchServersRefreshButton->setEnabled(false);
    }

    statusBar()->showMessage(QString("Getting %1...").arg(serverFilterLabel(m_serverGameFilter, m_serverTypeFilter).toLower()));
    m_bridge->requestServers({}, m_serverGameFilter, m_serverTypeFilter);
}
void MainWindow::rebuildServerFilterMenu()
{
    if (!m_serverFilterMenu || !m_serverFilterGroup) {
        return;
    }

    const auto existingActions = m_serverFilterGroup->actions();
    for (QAction *action : existingActions) {
        m_serverFilterGroup->removeAction(action);
    }
    m_serverFilterMenu->clear();

    auto addFilter = [this](const QString &label, quint32 gameId, quint32 type) {
        auto *action = m_serverFilterMenu->addAction(label);
        action->setIcon(legacyServerIcon(gameId, type));
        action->setCheckable(true);
        action->setChecked(gameId == m_serverGameFilter && type == m_serverTypeFilter);
        m_serverFilterGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, gameId, type]() {
            setServerFilter(gameId, type);
        });
    };

    addFilter("Game Servers", 0, kGameServerType);
    addFilter("DarkSpace", kDarkSpaceGameId, kGameServerType);

    if (!isStaffProfile()) {
        return;
    }

    m_serverFilterMenu->addSeparator();
    addFilter("DarkSpace Beta", kDarkSpaceBetaGameId, kGameServerType);
    addFilter("Profiler", 0, kProfilerServerType);
    addFilter("Process", 0, kProcessServerType);
    addFilter("Mirrors", 0, kMirrorServerType);
    addFilter("Meta", 0, kMetaServerType);
    addFilter("Subservers", 0, kGameSubServerType);
    m_serverFilterMenu->addSeparator();
    addFilter("Everything", 0, 0);
}

