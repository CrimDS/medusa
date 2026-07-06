#pragma once

#include "launcher/LaunchCatalog.h"
#include "net/GameProtocolConstants.h"
#include "net/MetaClientTypes.h"
#include "ui/LegacyIcons.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QString>
#include <QStyle>
#include <QWidget>
namespace gamecq::launch_ui {

constexpr int kLaunchIdRole = Qt::UserRole + 20;
constexpr int kLaunchKindRole = Qt::UserRole + 21;
constexpr int kLaunchGroupRole = Qt::UserRole + 22;
constexpr auto kDarkSpaceLogUrl = "https://www.darkspace.net/index.php?lang=en&module=log.php";

inline QString settingString(const QString &key, const QString &fallback = {})
{
    return QSettings().value(key, fallback).toString();
}

inline void removeLaunchArtworkVariants(const QString &folder, const QString &baseName)
{
    const QDir dir(folder);
    for (const QString &extension : launchArtworkExtensions()) {
        QFile::remove(dir.filePath(QString("%1.%2").arg(baseName, extension)));
    }
}

inline QString canonicalOrAbsolutePath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    if (!canonical.isEmpty()) {
        return QDir::cleanPath(canonical);
    }

    return QDir::cleanPath(info.absoluteFilePath());
}

inline bool isSamePath(const QString &left, const QString &right)
{
    return QDir::cleanPath(left).compare(QDir::cleanPath(right), Qt::CaseInsensitive) == 0;
}

inline bool isChildPathOf(const QString &child, const QString &parent)
{
    const QString cleanChild = QDir::cleanPath(child);
    QString cleanParent = QDir::cleanPath(parent);
    if (isSamePath(cleanChild, cleanParent)) {
        return true;
    }
    if (!cleanParent.endsWith('/')) {
        cleanParent += '/';
    }
    return cleanChild.startsWith(cleanParent, Qt::CaseInsensitive);
}

inline bool isSafeBuiltInDeleteRoot(const LaunchEntry &entry, const QString &root, QString *reason)
{
    if (root.trimmed().isEmpty()) {
        if (reason) {
            *reason = "The install folder is empty.";
        }
        return false;
    }

    const QFileInfo rootInfo(root);
    if (!rootInfo.exists()) {
        return true;
    }
    if (!rootInfo.isDir()) {
        if (reason) {
            *reason = "The selected path is not a folder.";
        }
        return false;
    }

    const QString cleanRoot = canonicalOrAbsolutePath(root);
    const QDir rootDir(cleanRoot);
    if (rootDir.isRoot()) {
        if (reason) {
            *reason = "Refusing to delete a drive root.";
        }
        return false;
    }

    const QString appDir = canonicalOrAbsolutePath(QCoreApplication::applicationDirPath());
    if (isSamePath(cleanRoot, appDir)) {
        if (reason) {
            *reason = "Refusing to delete the GameCQ application folder.";
        }
        return false;
    }

    if (isChildPathOf(cleanRoot, appDir)) {
        return true;
    }

    if (entry.httpManifestUpdater) {
        const QString folderName = QFileInfo(cleanRoot).fileName();
        if (folderName.contains("darkspace", Qt::CaseInsensitive) || folderName.contains("dark space", Qt::CaseInsensitive)) {
            return true;
        }
        if (reason) {
            *reason = "Custom D12 folders must be dedicated DarkSpace folders before GameCQ can remove them.";
        }
        return false;
    }

    if (reason) {
        *reason = "Refusing to delete files outside the portable GameCQ folder.";
    }
    return false;
}

inline void polishObjectName(QWidget *widget)
{
    if (!widget) {
        return;
    }

    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

inline void setPillLabel(QLabel *label, const QString &text, const QString &pillName, const QString &toolTip = {})
{
    if (!label) {
        return;
    }

    label->setText(text);
    label->setObjectName(pillName);
    label->setToolTip(toolTip);
    polishObjectName(label);
}

inline QLabel *makeSectionLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("sectionLabel");
    return label;
}

inline QFrame *makePanel(const QString &objectName)
{
    auto *panel = new QFrame;
    panel->setObjectName(objectName);
    panel->setFrameShape(QFrame::NoFrame);
    return panel;
}

inline bool copyLaunchArtwork(QWidget *parent, const LaunchEntry &entry, const QString &source, const QString &kind)
{
    if (source.trimmed().isEmpty()) {
        return true;
    }

    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        QMessageBox::warning(parent, "Artwork Not Found", QString("Could not find artwork file:\n\n%1").arg(QDir::toNativeSeparators(source)));
        return false;
    }

    QString extension = sourceInfo.suffix().toLower();
    if (extension.isEmpty()) {
        extension = "png";
    }

    const QString folder = launchArtworkFolder(entry, true);
    const QString destination = QDir(folder).filePath(QString("%1.%2").arg(kind, extension));
    const QString sourceCanonical = sourceInfo.canonicalFilePath();
    const QString destinationCanonical = QFileInfo(destination).canonicalFilePath();
    if (!destinationCanonical.isEmpty() && sourceCanonical.compare(destinationCanonical, Qt::CaseInsensitive) == 0) {
        return true;
    }

    removeLaunchArtworkVariants(folder, kind);
    if (!QFile::copy(source, destination)) {
        QMessageBox::warning(parent, "Artwork Copy Failed", QString("Could not copy artwork to:\n\n%1").arg(QDir::toNativeSeparators(destination)));
        return false;
    }

    return true;
}

inline QString serverFilterLabel(quint32 gameId, quint32 type)
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

inline bool isPublicServerFilter(quint32 gameId, quint32 type)
{
    return type == kGameServerType && (gameId == 0 || gameId == kDarkSpaceGameId);
}

inline bool isPublicServerRow(const ServerInfo &server)
{
    return server.type == kGameServerType && server.gameId == kDarkSpaceGameId;
}

inline QIcon legacyToolIconForText(const QString &text)
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

} // namespace
