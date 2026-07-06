#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

struct LaunchEntry {
    // Stable catalog identity. Built-in IDs should not change after release;
    // user settings, last-used timestamps, artwork, and ordering depend on it.
    QString id;
    QString name;
    QString variant;
    QString category;
    QString source;
    QString executable;
    QString commandLine;
    QString updater;
    QString workingDirectory;
    QString manifestUrl;
    quint32 lobbyId = 0;
    quint32 serverType = 0;
    bool needsServer = false;
    bool staffOnly = false;
    bool httpManifestUpdater = false;
    bool stubOnly = false;
    bool customEntry = false;
    QString description;
    QString kind;
    QString steamAppId;
    QString serverProvider;
    QString group;
};

// Launch catalog helpers are intentionally centralized so the UI does not
// duplicate path resolution, built-in/custom merging, display naming, artwork
// lookup, or DarkSpace-specific utility folding rules.
bool hasLaunchEntry(const LaunchEntry &entry);
QString normalizedLaunchKind(QString kind);
QString launchKindForEntry(const LaunchEntry &entry);
QString launchKindLabel(const QString &kind);
QString normalizedLaunchCategory(QString category, const QString &kind = {});
QString normalizedLaunchPlatform(QString platform);
QString launchPlatformForEntry(const LaunchEntry &entry);
QString detectedLaunchPlatform(const QString &executable);
QString launchGroupForEntry(const LaunchEntry &entry);
QString launchSubtitleText(const LaunchEntry &entry);

QString d12InstallRoot();
QString d9InstallRoot();
QList<LaunchEntry> builtinLaunchCatalog();
QString generatedCustomLaunchId(const QString &executable, const QString &name);
QString newCustomLaunchId();
void writeCustomLaunchCatalog(const QList<LaunchEntry> &entries);
QList<LaunchEntry> customLaunchCatalog();
QList<LaunchEntry> launchCatalog();

bool containsCaseInsensitive(const QStringList &values, const QString &candidate);
void addUniqueCaseInsensitive(QStringList &values, const QString &candidate);
QStringList savedLaunchLibraryGroups();
void saveLaunchLibraryGroups(QStringList groups);
QStringList launchLibraryGroups();
QStringList launchLibraryOrder();
void saveLaunchLibraryOrder(const QStringList &order);
QDateTime launchLastUsed(const QString &id);
void recordLaunchLastUsed(const QString &id);
int launchOrderIndex(const QStringList &order, const QString &id);
QString launchLibraryDisplayName(const LaunchEntry &entry);
LaunchEntry findLaunchEntry(const QString &id);
bool usesLegacyMirrorUpdater(const LaunchEntry &entry);
bool hasLaunchUpdater(const LaunchEntry &entry);
bool isFoldedDarkSpaceUtility(const LaunchEntry &entry);
QString relatedDarkSpaceLaunchId(const LaunchEntry &entry, const QString &kind);
bool deleteCustomLaunchEntry(const QString &id);

QStringList legacyLaunchRoots();
QString resolveLaunchPath(const LaunchEntry &entry, const QString &path);
QString launchWorkingDirectory(const LaunchEntry &entry, const QString &executable);
QString launchInstallRoot(const LaunchEntry &entry);
QString launchArtRoot();
QString defaultLaunchArtRoot();
QStringList launchArtRoots();
QString primaryLaunchArtKey(const LaunchEntry &entry);
QStringList launchArtKeys(const LaunchEntry &entry);
QString launchArtworkFolder(const LaunchEntry &entry, bool create = false);
QStringList launchArtworkExtensions();
QString findLaunchArtwork(const LaunchEntry &entry, const QStringList &baseNames);
QString findLaunchMetadataFile(const LaunchEntry &entry, const QStringList &fileNames);
QString launchDescriptionForDisplay(const LaunchEntry &entry);
QString launchDescriptionText(const LaunchEntry &entry);
QString substitutedCommandLine(const LaunchEntry &entry, const QString &address, int port, quint32 sessionId);
