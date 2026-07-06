#pragma once

#include "launcher/LaunchCatalog.h"
#include "net/MetaClientTypes.h"

#include <Qt>
#include <QString>

class QListWidget;
class QListWidgetItem;

constexpr int kServerAddressRole = Qt::UserRole + 30;
constexpr int kServerPortRole = Qt::UserRole + 31;
constexpr int kServerGameIdRole = Qt::UserRole + 32;
constexpr int kServerTypeRole = Qt::UserRole + 33;
constexpr int kServerNameRole = Qt::UserRole + 34;
constexpr int kServerSummaryRole = Qt::UserRole + 35;
constexpr int kServerDetailsRole = Qt::UserRole + 36;
constexpr int kLaunchInfoUrlRole = Qt::UserRole + 37;
constexpr int kLaunchInfoTitleRole = Qt::UserRole + 38;
constexpr int kLaunchInfoPreviewRole = Qt::UserRole + 39;
constexpr int kLaunchInfoBodyRole = Qt::UserRole + 40;

void addLaunchInfoItem(QListWidget *list, const QString &title, const QString &body, const QString &toolTip = {});
int populateLaunchInfoItems(QListWidget *list, const LaunchEntry &entry);
void updateLaunchInfoCardExpansion(QListWidget *list);
void addLaunchServerItem(QListWidget *list, const ServerInfo &server, bool gamecqServer, bool showEndpoint);
ServerInfo launchServerInfoFromItem(const QListWidgetItem *item);
bool hasLaunchServerEndpoint(const QListWidgetItem *item);
