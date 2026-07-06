#include "ui/LaunchInfoList.h"

#include "launcher/LaunchMetadata.h"
#include "net/GameProtocolConstants.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QRect>
#include <QSize>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace {

void polishObjectName(QWidget *widget)
{
    if (!widget) {
        return;
    }

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}


QString serverPopulationPillName(const QString &population)
{
    const QString lower = population.toLower();
    if (lower.contains("perfect") || lower.contains("online") || lower.contains("low")) {
        return "pillReady";
    }
    if (lower.contains("medium")) {
        return "pillUpdating";
    }
    if (lower.contains("full") || lower.contains("high")) {
        return "pillFull";
    }
    return "pillNeutral";
}

QString serverTypeName(quint32 gameId, quint32 type)
{
    switch (type) {
    case kMetaServerType:
        return "Meta Server";
    case kMirrorServerType:
        return "Mirror Server";
    case kGameServerType:
        return gameId == kDarkSpaceBetaGameId ? "DarkSpace Beta" : "DarkSpace";
    case kGameSubServerType:
        return "Game Subserver";
    case kProcessServerType:
        return "Process Client";
    case kProfilerServerType:
        return "Profiler Client";
    default:
        return QString("Type %1").arg(type);
    }
}

QString endpointText(const ServerInfo &server)
{
    if (server.address.isEmpty() || server.port == 0) {
        return {};
    }

    return QString("%1:%2").arg(server.address).arg(server.port);
}

QString compactServerSummary(const ServerInfo &server, int limit = 150)
{
    QString text = server.description.trimmed();
    if (text.isEmpty()) {
        return {};
    }

    text.replace("\r\n", "\n");
    text.replace('\r', '\n');

    QStringList candidates;
    const QString shortDescription = server.shortDescription.trimmed();
    for (QString line : text.split('\n')) {
        line = line.simplified();
        if (line.isEmpty()) {
            continue;
        }
        if (!shortDescription.isEmpty()
            && (line.compare(shortDescription, Qt::CaseInsensitive) == 0
                || line.startsWith(shortDescription, Qt::CaseInsensitive))) {
            continue;
        }
        if (line.endsWith(':') && line.size() < 18) {
            continue;
        }
        candidates << line;
    }

    QString summary = candidates.join(' ').simplified();
    if (summary.isEmpty() || (!shortDescription.isEmpty() && summary.compare(shortDescription, Qt::CaseInsensitive) == 0)) {
        return {};
    }
    if (summary.size() <= limit) {
        return summary;
    }

    int cut = summary.lastIndexOf(' ', limit);
    if (cut < 80) {
        cut = limit;
    }
    return summary.left(cut).trimmed() + "...";
}
} // namespace

void addLaunchInfoItem(QListWidget *list, const QString &title, const QString &body, const QString &toolTip)
{
    if (!list || title.trimmed().isEmpty()) {
        return;
    }

    const QString cleanTitle = title.trimmed();
    const QString cleanBody = body.trimmed();
    const QString preview = compactPlainText(cleanBody, 220);
    auto *item = new QListWidgetItem(cleanTitle, list);
    item->setData(kLaunchInfoTitleRole, cleanTitle);
    item->setData(kLaunchInfoPreviewRole, preview);
    item->setData(kLaunchInfoBodyRole, cleanBody);

    const QUrl target = QUrl::fromUserInput(toolTip.trimmed());
    const bool isArticle = target.isValid()
        && (target.scheme().compare("http", Qt::CaseInsensitive) == 0
            || target.scheme().compare("https", Qt::CaseInsensitive) == 0);
    if (isArticle) {
        item->setData(kLaunchInfoUrlRole, target);
        item->setToolTip(QString("Double-click to open in Browser\n%1").arg(target.toString()));
    }

    auto *card = new QWidget(list);
    card->setObjectName("launchServerCard");
    card->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(5);

    auto *name = new QLabel(cleanTitle, card);
    name->setObjectName("launchServerName");
    name->setWordWrap(true);
    cardLayout->addWidget(name);

    auto *bodyLabel = new QLabel(preview, card);
    bodyLabel->setObjectName("launchServerDescription");
    bodyLabel->setWordWrap(true);
    bodyLabel->setVisible(!preview.isEmpty());
    cardLayout->addWidget(bodyLabel);
    cardLayout->addStretch(1);

    item->setSizeHint(QSize(0, preview.isEmpty() ? 58 : 86));
    if (!isArticle && !toolTip.trimmed().isEmpty()) {
        item->setToolTip(toolTip.trimmed());
    }

    list->setItemWidget(item, card);
}

int populateLaunchInfoItems(QListWidget *list, const LaunchEntry &entry)
{
    if (!list) {
        return 0;
    }

    int count = 0;
    const QString newsPath = findLaunchMetadataFile(entry, {"news.json"});
    const QJsonObject news = newsPath.isEmpty() ? QJsonObject() : readLaunchJsonObject(newsPath);
    const QJsonArray newsItems = news.value("items").toArray();
    int addedNews = 0;
    for (int i = 0; i < newsItems.size() && addedNews < 5; ++i) {
        const QJsonObject item = newsItems.at(i).toObject();
        if (!isLikelyEnglishSteamNews(item)) {
            continue;
        }

        QString title = item.value("title").toString().trimmed();
        if (title.isEmpty()) {
            title = QString("Steam update %1").arg(addedNews + 1);
        }

        QStringList bodyParts;
        const qint64 date = item.value("date").toInteger();
        if (date > 0) {
            bodyParts << QDateTime::fromSecsSinceEpoch(date).toLocalTime().date().toString(Qt::ISODate);
        }
        const QString feedLabel = item.value("feedlabel").toString().trimmed();
        if (!feedLabel.isEmpty() && isLikelyEnglishSteamNews(QJsonObject{{"title", feedLabel}})) {
            bodyParts << feedLabel;
        }
        const QString contents = plainLaunchHtmlText(item.value("contents").toString(), 1800);
        if (!contents.isEmpty() && isLikelyEnglishSteamNews(QJsonObject{{"title", contents.left(160)}})) {
            bodyParts << contents;
        }
        const QString url = item.value("url").toString().trimmed();

        addLaunchInfoItem(list, title, bodyParts.join("\n"), url);
        ++count;
        ++addedNews;
    }

    if (count == 0) {
        addLaunchInfoItem(
            list,
            "No updates or news yet",
            "Use Identify to search for available game metadata.",
            launchArtworkFolder(entry));
        ++count;
    }

    return count;
}

void updateLaunchInfoCardExpansion(QListWidget *list)
{
    if (!list) {
        return;
    }

    const int textWidth = qMax(180, list->viewport()->width() - 48);
    for (int row = 0; row < list->count(); ++row) {
        QListWidgetItem *item = list->item(row);
        if (!item || !item->data(kLaunchInfoTitleRole).isValid()) {
            continue;
        }

        QWidget *card = list->itemWidget(item);
        if (!card) {
            continue;
        }

        const bool expanded = item == list->currentItem();
        const QString preview = item->data(kLaunchInfoPreviewRole).toString();
        const QString body = item->data(kLaunchInfoBodyRole).toString();
        const QString text = expanded && !body.isEmpty() ? body : preview;

        auto *bodyLabel = card->findChild<QLabel *>("launchServerDescription");
        if (bodyLabel) {
            bodyLabel->setText(text);
            bodyLabel->setVisible(!text.isEmpty());
        }

        int height = text.isEmpty() ? 58 : 86;
        if (expanded && !text.isEmpty()) {
            const QFontMetrics metrics(bodyLabel ? bodyLabel->font() : list->font());
            const QRect textBounds = metrics.boundingRect(
                QRect(0, 0, textWidth, 12000),
                Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop,
                text);
            height = qBound(86, 62 + textBounds.height(), 420);
        }

        item->setSizeHint(QSize(0, height));
        card->setProperty("expanded", expanded);
        polishObjectName(card);
    }

    list->doItemsLayout();
    list->viewport()->update();
}
void addLaunchServerItem(QListWidget *list, const ServerInfo &server, bool gamecqServer, bool showEndpoint)
{
    if (!list) {
        return;
    }

    const QString description = server.description.isEmpty() ? server.shortDescription : server.description;
    const QString summary = compactServerSummary(server);
    const QString details = description.trimmed();
    const QString endpoint = endpointText(server);
    const QString occupancy = server.maxClients > 0
        ? QString("%1 / %2").arg(server.clients).arg(server.maxClients)
        : server.population.trimmed();

    QStringList metaParts;
    if (!server.shortDescription.isEmpty()) {
        metaParts << server.shortDescription;
    }
    if (!server.population.isEmpty() && server.population != occupancy) {
        metaParts << server.population;
    }
    if (gamecqServer) {
        metaParts << serverTypeName(server.gameId, server.type);
    }
    if (!endpoint.isEmpty() && showEndpoint) {
        metaParts << endpoint;
    }

    auto *item = new QListWidgetItem(server.name.isEmpty() ? endpoint : server.name, list);
    item->setData(kServerNameRole, server.name);
    item->setData(kServerAddressRole, server.address);
    item->setData(kServerPortRole, server.port);
    item->setData(kServerGameIdRole, server.gameId);
    item->setData(kServerTypeRole, server.type);
    item->setData(kServerSummaryRole, summary);
    item->setData(kServerDetailsRole, details);
    item->setSizeHint(QSize(0, summary.isEmpty() ? 78 : 112));

    auto *card = new QWidget(list);
    card->setObjectName("launchServerCard");
    card->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(5);

    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(8);

    auto *name = new QLabel(server.name.isEmpty() ? endpoint : server.name, card);
    name->setObjectName("launchServerName");
    name->setWordWrap(false);
    titleRow->addWidget(name, 1);

    if (!occupancy.isEmpty()) {
        auto *population = new QLabel(occupancy, card);
        population->setObjectName(serverPopulationPillName(server.population));
        population->setAlignment(Qt::AlignCenter);
        population->setToolTip(server.maxClients > 0
                                   ? QString("%1 players online out of %2 slots.").arg(server.clients).arg(server.maxClients)
                                   : server.population);
        titleRow->addWidget(population, 0, Qt::AlignTop);
    }
    cardLayout->addLayout(titleRow);

    if (!metaParts.isEmpty()) {
        auto *meta = new QLabel(metaParts.join("  -  "), card);
        meta->setObjectName("launchServerMeta");
        meta->setWordWrap(true);
        cardLayout->addWidget(meta);
    }

    auto *body = new QLabel(summary, card);
    body->setObjectName("launchServerDescription");
    body->setWordWrap(true);
    body->setVisible(!summary.isEmpty());
    cardLayout->addWidget(body);

    cardLayout->addStretch(1);

    QStringList tooltipParts;
    tooltipParts << (server.name.isEmpty() ? endpoint : server.name);
    if (!metaParts.isEmpty()) {
        tooltipParts << metaParts.join("  -  ");
    }
    if (!description.isEmpty()) {
        tooltipParts << description;
    }
    if (!endpoint.isEmpty() && !tooltipParts.contains(endpoint)) {
        tooltipParts << endpoint;
    }
    item->setToolTip(tooltipParts.join('\n'));
    list->setItemWidget(item, card);
}

ServerInfo launchServerInfoFromItem(const QListWidgetItem *item)
{
    ServerInfo server;
    if (!item) {
        return server;
    }

    server.name = item->data(kServerNameRole).toString();
    server.address = item->data(kServerAddressRole).toString();
    server.port = item->data(kServerPortRole).toInt();
    server.gameId = item->data(kServerGameIdRole).toUInt();
    server.type = item->data(kServerTypeRole).toUInt();
    return server;
}

bool hasLaunchServerEndpoint(const QListWidgetItem *item)
{
    const ServerInfo server = launchServerInfoFromItem(item);
    return !server.address.isEmpty() && server.port != 0;
}
