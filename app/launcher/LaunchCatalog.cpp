#include "launcher/LaunchCatalog.h"

#include "net/GameProtocolConstants.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QRegularExpression>
#include <QSettings>
#include <QUuid>

#include <limits>

namespace {

constexpr auto kLegacyD12InstallRoot = "C:/Projects/Darkspace D12 Testing/DS Live - Official";
// D12 and GameCQ 1 ⅜ updates must stay off the official legacy update path.
constexpr auto kD12ManifestUrl = "https://jack-online.co.uk/ds/dist/manifest.json";

QString safeLaunchFileNamePart(QString value, const QString &fallback)
{
    value = value.trimmed();
    if (value.isEmpty()) {
        value = fallback;
    }
    value.replace(QRegularExpression("[^A-Za-z0-9._@!#$%&()\\[\\]^`~-]+"), "_");
    value.replace(QRegularExpression("_+"), "_");
    return value.left(80);
}

} // namespace
bool hasLaunchEntry(const LaunchEntry &entry)
{
    return !entry.id.isEmpty();
}

QString normalizedLaunchKind(QString kind)
{
    kind = kind.trimmed().toLower();
    if (kind == "software" || kind == "app" || kind == "apps" || kind == "program" || kind == "programs" || kind == "tool" || kind == "tools") {
        return "software";
    }
    if (kind == "game" || kind == "games") {
        return "game";
    }
    return {};
}

QString launchKindForEntry(const LaunchEntry &entry)
{
    const QString explicitKind = normalizedLaunchKind(entry.kind);
    if (!explicitKind.isEmpty()) {
        return explicitKind;
    }

    const QString text = QString("%1 %2 %3").arg(entry.category, entry.name, entry.variant).toLower();
    if (text.contains("software") || text.contains("program") || text.contains("tool") || text.contains("resourcer")) {
        return "software";
    }
    return "game";
}

QString launchKindLabel(const QString &kind)
{
    return normalizedLaunchKind(kind) == "software" ? QString("Software") : QString("Games");
}

QString normalizedLaunchCategory(QString category, const QString &kind)
{
    const QString trimmed = category.trimmed();
    if (trimmed.compare("Games", Qt::CaseInsensitive) == 0
        || trimmed.compare("Game", Qt::CaseInsensitive) == 0
        || trimmed.compare("Community Games", Qt::CaseInsensitive) == 0) {
        return "Games";
    }
    if (trimmed.compare("Tools", Qt::CaseInsensitive) == 0
        || trimmed.compare("Tool", Qt::CaseInsensitive) == 0) {
        return "Tools";
    }
    if (trimmed.compare("Software", Qt::CaseInsensitive) == 0
        || trimmed.compare("Program", Qt::CaseInsensitive) == 0
        || trimmed.compare("Programs", Qt::CaseInsensitive) == 0) {
        return "Software";
    }
    return normalizedLaunchKind(kind) == "software" ? QString("Software") : QString("Games");
}

QString normalizedLaunchPlatform(QString platform)
{
    const QString value = platform.trimmed();
    if (value.isEmpty() || value.compare("Local", Qt::CaseInsensitive) == 0
        || value.compare("Local App", Qt::CaseInsensitive) == 0
        || value.compare("Community", Qt::CaseInsensitive) == 0) {
        return "Standalone";
    }
    if (value.compare("Epic", Qt::CaseInsensitive) == 0
        || value.compare("Epic Games Store", Qt::CaseInsensitive) == 0) {
        return "Epic Games";
    }
    if (value.compare("GoG", Qt::CaseInsensitive) == 0
        || value.compare("GOG Galaxy", Qt::CaseInsensitive) == 0) {
        return "GOG";
    }
    if (value.compare("Xbox", Qt::CaseInsensitive) == 0
        || value.compare("Microsoft Store", Qt::CaseInsensitive) == 0
        || value.compare("Windows Store", Qt::CaseInsensitive) == 0) {
        return "Xbox / Windows";
    }
    return value;
}

QString launchPlatformForEntry(const LaunchEntry &entry)
{
    if (!entry.customEntry) {
        return "Official";
    }
    return normalizedLaunchPlatform(entry.source);
}

QString detectedLaunchPlatform(const QString &executable)
{
    const QString path = QDir::fromNativeSeparators(executable).toLower();
    if (path.contains("/steamapps/common/")) {
        return "Steam";
    }
    if (path.contains("/epic games/") || path.contains("/epicgames/")) {
        return "Epic Games";
    }
    if (path.contains("/gog galaxy/games/") || path.contains("/gog games/")) {
        return "GOG";
    }
    if (path.contains("/xboxgames/") || path.contains("/windowsapps/")) {
        return "Xbox / Windows";
    }
    return {};
}

QString launchGroupForEntry(const LaunchEntry &entry)
{
    const QString group = entry.group.trimmed();
    if (!group.isEmpty()) {
        return group;
    }
    return normalizedLaunchCategory(entry.category, launchKindForEntry(entry));
}

QString launchSubtitleText(const LaunchEntry &entry)
{
    const QString category = normalizedLaunchCategory(entry.category, launchKindForEntry(entry));
    return category.isEmpty() ? launchKindLabel(launchKindForEntry(entry)) : category;
}

QString d12InstallRoot()
{
    const QString portableRoot = QDir(QCoreApplication::applicationDirPath()).filePath("DarkSpaceD12");
    const QString configured = QDir::cleanPath(QDir::fromNativeSeparators(QSettings().value("Launch/D12InstallRoot").toString()));
    if (configured.isEmpty() || configured.compare(QDir::cleanPath(QString(kLegacyD12InstallRoot)), Qt::CaseInsensitive) == 0) {
        return QDir::cleanPath(portableRoot);
    }

    return configured;
}

QString d9InstallRoot()
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(".Cache/DarkSpace"));
}

QList<LaunchEntry> builtinLaunchCatalog()
{
    return {
        {"darkspace-d9-live", "DarkSpace", "Original D9 x86 - Live", "Games", "Legacy GCQL", "DarkSpaceClient.exe", "$ADDRESS $PORT $SID", {}, d9InstallRoot(), {}, kDarkSpaceGameId, kGameServerType, true, false, false},
        {"darkspace-d9-setup", "DarkSpace Setup", "Original D9 x86", "Games", "Legacy GCQL", "DarkSpaceSetup.exe", {}, {}, d9InstallRoot(), {}, kDarkSpaceGameId, 0, false, false, false},
        {"darkspace-d9-tutorial", "DarkSpace Tutorial", "Original D9 x86", "Games", "Legacy GCQL", "DarkSpaceClient.exe", "-tutorial", {}, d9InstallRoot(), {}, kDarkSpaceGameId, 0, false, false, false},
        {"darkspace-d12-live", "DarkSpace", "D12 x64 - Live Test", "Games", "Community Manifest", "DarkSpaceClient.exe", "$ADDRESS $PORT $SID", {}, d12InstallRoot(), kD12ManifestUrl, kDarkSpaceGameId, kGameServerType, true, false, true},
        {"darkspace-d12-setup", "DarkSpace Setup", "D12 x64 - Live Test", "Games", "Community Manifest", "DarkSpaceSetup.exe", {}, {}, d12InstallRoot(), kD12ManifestUrl, kDarkSpaceGameId, 0, false, false, true},
        {"darkspace-d12-tutorial", "DarkSpace Tutorial", "D12 x64 - Live Test", "Games", "Community Manifest", "DarkSpaceClient.exe", "-tutorial", {}, d12InstallRoot(), kD12ManifestUrl, kDarkSpaceGameId, 0, false, false, true},
        {"darkspace-d9-beta", "DarkSpace", "Original D9 x86 - Beta", "Games", "Stubbed", "DarkSpaceBeta/DarkSpaceClient.exe", "$ADDRESS $PORT $SID", {}, "DarkSpaceBeta", {}, kDarkSpaceBetaGameId, kGameServerType, true, true, false, true},
        {"darkspace-d9-beta-setup", "DarkSpace Setup", "Original D9 x86 - Beta", "Games", "Stubbed", "DarkSpaceBeta/DarkSpaceSetup.exe", {}, {}, "DarkSpaceBeta", {}, kDarkSpaceBetaGameId, 0, false, true, false, true},
        {"darkspace-d9-beta-tutorial", "DarkSpace Tutorial", "Original D9 x86 - Beta", "Games", "Stubbed", "DarkSpaceBeta/DarkSpaceClient.exe", "-tutorial", {}, "DarkSpaceBeta", {}, kDarkSpaceBetaGameId, 0, false, true, false, true},
        {"resourcer", "Resourcer", "Tools", "Tools", "Legacy GCQL", "Resourcer/Resourcer.exe", {}, {}, "Resourcer", {}, kDarkSpaceBetaGameId, 0, false, true, false},
    };
}

QString generatedCustomLaunchId(const QString &executable, const QString &name)
{
    const QByteArray seed = QString("%1|%2")
                                .arg(QDir::cleanPath(executable).toLower(), name.trimmed().toLower())
                                .toUtf8();
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(seed, QCryptographicHash::Sha1).toHex().left(12));
    return QString("custom-legacy-%1").arg(digest);
}

QString newCustomLaunchId()
{
    return QString("custom-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void writeCustomLaunchCatalog(const QList<LaunchEntry> &entries)
{
    QSettings settings;
    settings.beginGroup("Launch");
    settings.remove("CustomEntries");
    settings.beginWriteArray("CustomEntries", entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        settings.setArrayIndex(i);
        const LaunchEntry &entry = entries[i];
        settings.setValue("id", entry.id);
        settings.setValue("name", entry.name);
        settings.setValue("variant", entry.variant.isEmpty() ? launchPlatformForEntry(entry) : entry.variant);
        const QString kind = launchKindForEntry(entry);
        settings.setValue("category", normalizedLaunchCategory(entry.category, kind));
        settings.setValue("kind", kind);
        settings.setValue("source", launchPlatformForEntry(entry));
        settings.setValue("executable", QDir::cleanPath(entry.executable));
        settings.setValue("workingDirectory", QDir::cleanPath(entry.workingDirectory));
        settings.setValue("arguments", entry.commandLine);
        settings.setValue("description", entry.description);
        settings.setValue("steamAppId", entry.steamAppId.trimmed());
        settings.setValue("serverProvider", entry.serverProvider.trimmed());
        settings.setValue("group", entry.group.trimmed());
    }
    settings.endArray();
    settings.endGroup();
}

QList<LaunchEntry> customLaunchCatalog()
{
    QList<LaunchEntry> entries;

    QSettings settings;
    settings.beginGroup("Launch");
    const int count = settings.beginReadArray("CustomEntries");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);

        const QString executable = settings.value("executable").toString();
        if (executable.trimmed().isEmpty()) {
            continue;
        }

        LaunchEntry entry;
        entry.name = settings.value("name", QFileInfo(executable).completeBaseName()).toString();
        const QString storedId = settings.value("id").toString().trimmed();
        entry.id = storedId.isEmpty() ? generatedCustomLaunchId(executable, entry.name) : storedId;
        entry.variant = settings.value("variant", "Standalone").toString();
        entry.kind = normalizedLaunchKind(settings.value("kind").toString());
        const QString storedCategory = settings.value("category", "Games").toString();
        entry.category = normalizedLaunchCategory(storedCategory, entry.kind);
        entry.source = normalizedLaunchPlatform(settings.value("source", "Standalone").toString());
        entry.executable = executable;
        entry.workingDirectory = settings.value("workingDirectory", QFileInfo(executable).absolutePath()).toString();
        entry.commandLine = settings.value("arguments").toString();
        entry.customEntry = true;
        entry.description = settings.value("description").toString();
        entry.steamAppId = settings.value("steamAppId").toString().trimmed();
        entry.serverProvider = settings.value("serverProvider").toString().trimmed().toLower();
        entry.group = settings.value("group").toString().trimmed();
        if (entry.group.isEmpty()
            && storedCategory.compare("Games", Qt::CaseInsensitive) != 0
            && storedCategory.compare("Software", Qt::CaseInsensitive) != 0
            && storedCategory.compare("Tools", Qt::CaseInsensitive) != 0
            && storedCategory.compare("Steam", Qt::CaseInsensitive) != 0) {
            entry.group = storedCategory.trimmed();
        }
        entries.append(entry);
    }
    settings.endArray();
    settings.endGroup();

    return entries;
}

QList<LaunchEntry> launchCatalog()
{
    QList<LaunchEntry> entries = builtinLaunchCatalog();
    entries.append(customLaunchCatalog());
    return entries;
}

bool containsCaseInsensitive(const QStringList &values, const QString &candidate)
{
    for (const QString &value : values) {
        if (value.compare(candidate, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

void addUniqueCaseInsensitive(QStringList &values, const QString &candidate)
{
    const QString trimmed = candidate.trimmed();
    if (!trimmed.isEmpty() && !containsCaseInsensitive(values, trimmed)) {
        values.append(trimmed);
    }
}

QStringList savedLaunchLibraryGroups()
{
    QStringList groups = QSettings().value("Launch/LibraryGroups").toStringList();
    groups.removeAll({});
    groups.removeDuplicates();
    return groups;
}

void saveLaunchLibraryGroups(QStringList groups)
{
    for (QString &group : groups) {
        group = normalizedLaunchCategory(group);
    }
    groups.removeAll({});
    groups.removeDuplicates();
    QSettings().setValue("Launch/LibraryGroups", groups);
}

QStringList launchLibraryGroups()
{
    QStringList groups;
    addUniqueCaseInsensitive(groups, "Games");
    addUniqueCaseInsensitive(groups, "Software");
    addUniqueCaseInsensitive(groups, "Tools");
    for (const QString &group : savedLaunchLibraryGroups()) {
        addUniqueCaseInsensitive(groups, normalizedLaunchCategory(group));
    }
    for (const LaunchEntry &entry : launchCatalog()) {
        addUniqueCaseInsensitive(groups, launchGroupForEntry(entry));
    }
    groups.sort(Qt::CaseInsensitive);
    return groups;
}

QStringList launchLibraryOrder()
{
    return QSettings().value("Launch/LibraryOrder").toStringList();
}

void saveLaunchLibraryOrder(const QStringList &order)
{
    QStringList unique;
    for (const QString &id : order) {
        addUniqueCaseInsensitive(unique, id);
    }
    QSettings().setValue("Launch/LibraryOrder", unique);
}

QDateTime launchLastUsed(const QString &id)
{
    return QSettings().value(QString("Launch/LastUsed/%1").arg(id)).toDateTime();
}

void recordLaunchLastUsed(const QString &id)
{
    if (!id.isEmpty()) {
        QSettings().setValue(QString("Launch/LastUsed/%1").arg(id), QDateTime::currentDateTimeUtc());
    }
}

int launchOrderIndex(const QStringList &order, const QString &id)
{
    const int index = order.indexOf(id);
    return index >= 0 ? index : (std::numeric_limits<int>::max)() / 2;
}

QString launchLibraryDisplayName(const LaunchEntry &entry)
{
    if (entry.id == "darkspace-d9-live") {
        return "DarkSpace - D9";
    }
    if (entry.id == "darkspace-d12-live") {
        return "DarkSpace - D12";
    }
    if (entry.id == "darkspace-d9-beta") {
        return "DarkSpace Beta - D9";
    }
    return entry.name;
}

LaunchEntry findLaunchEntry(const QString &id)
{
    const QList<LaunchEntry> catalog = launchCatalog();
    for (const LaunchEntry &entry : catalog) {
        if (entry.id == id) {
            return entry;
        }
    }
    return {};
}

bool usesLegacyMirrorUpdater(const LaunchEntry &entry)
{
    return entry.id.startsWith("darkspace-d9-") && !entry.stubOnly;
}

bool hasLaunchUpdater(const LaunchEntry &entry)
{
    return entry.httpManifestUpdater || usesLegacyMirrorUpdater(entry);
}

bool isFoldedDarkSpaceUtility(const LaunchEntry &entry)
{
    return entry.id.startsWith("darkspace-") && (entry.id.endsWith("-setup") || entry.id.endsWith("-tutorial"));
}

QString relatedDarkSpaceLaunchId(const LaunchEntry &entry, const QString &kind)
{
    if (entry.id == "darkspace-d9-live") {
        return QString("darkspace-d9-%1").arg(kind);
    }
    if (entry.id == "darkspace-d12-live") {
        return QString("darkspace-d12-%1").arg(kind);
    }
    if (entry.id == "darkspace-d9-beta") {
        return QString("darkspace-d9-beta-%1").arg(kind);
    }
    return {};
}

bool deleteCustomLaunchEntry(const QString &id)
{
    QList<LaunchEntry> entries = customLaunchCatalog();
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].id != id) {
            continue;
        }
        entries.removeAt(i);
        writeCustomLaunchCatalog(entries);
        return true;
    }

    return false;
}

QStringList legacyLaunchRoots()
{
    return {
        QCoreApplication::applicationDirPath(),
        QDir::currentPath(),
        "C:/Program Files/Palestar/GameCQ",
        "C:/Program Files (x86)/Palestar/GameCQ",
        "C:/GameCQ",
    };
}

QString resolveLaunchPath(const LaunchEntry &entry, const QString &path)
{
    const QString normalized = QDir::fromNativeSeparators(path);
    if (normalized.isEmpty()) {
        return {};
    }

    if (QDir::isAbsolutePath(normalized)) {
        return QDir::cleanPath(normalized);
    }

    if (!entry.workingDirectory.isEmpty()) {
        const QString root = QDir::isAbsolutePath(entry.workingDirectory)
                                 ? entry.workingDirectory
                                 : QDir(legacyLaunchRoots().constFirst()).filePath(entry.workingDirectory);
        const QString candidate = QDir(root).filePath(normalized);
        if (QFileInfo::exists(candidate) || entry.httpManifestUpdater) {
            return QDir::cleanPath(candidate);
        }
    }

    for (const QString &root : legacyLaunchRoots()) {
        const QString candidate = QDir(root).filePath(normalized);
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir::cleanPath(QDir(legacyLaunchRoots().constFirst()).filePath(normalized));
}

QString launchWorkingDirectory(const LaunchEntry &entry, const QString &executable)
{
    if (!entry.workingDirectory.isEmpty()) {
        const QString resolved = resolveLaunchPath(entry, entry.workingDirectory);
        if (QFileInfo(resolved).isDir()) {
            return resolved;
        }
    }
    return QFileInfo(executable).absolutePath();
}

QString launchInstallRoot(const LaunchEntry &entry)
{
    if (!entry.workingDirectory.isEmpty()) {
        if (QDir::isAbsolutePath(entry.workingDirectory)) {
            return QDir::cleanPath(entry.workingDirectory);
        }
        return resolveLaunchPath(entry, entry.workingDirectory);
    }

    const QString executable = resolveLaunchPath(entry, entry.executable);
    return QFileInfo(executable).absolutePath();
}

QString launchArtRoot()
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath("LaunchArt"));
}

QString defaultLaunchArtRoot()
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath("DefaultLaunchArt"));
}

QStringList launchArtRoots()
{
    QStringList roots;
    auto addRoot = [&roots](const QString &path) {
        const QString clean = QDir::cleanPath(path);
        if (!clean.isEmpty() && !roots.contains(clean, Qt::CaseInsensitive)) {
            roots << clean;
        }
    };

    addRoot(launchArtRoot());
    addRoot(QDir(QDir::currentPath()).filePath("LaunchArt"));
    addRoot(defaultLaunchArtRoot());

    QDir probe(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(probe.filePath("app/ui/MainWindow.cpp"))) {
            addRoot(probe.filePath("LaunchArt"));
            addRoot(probe.filePath("app/res/default_launch_art"));
            break;
        }
        if (!probe.cdUp()) {
            break;
        }
    }

    return roots;
}

QString primaryLaunchArtKey(const LaunchEntry &entry)
{
    return safeLaunchFileNamePart(entry.id, safeLaunchFileNamePart(entry.name, "launch-entry"));
}

QStringList launchArtKeys(const LaunchEntry &entry)
{
    QStringList keys;
    auto addKey = [&keys](const QString &value) {
        const QString key = safeLaunchFileNamePart(value, {}).trimmed();
        if (!key.isEmpty() && !keys.contains(key, Qt::CaseInsensitive)) {
            keys << key;
        }
    };

    addKey(entry.id);
    addKey(entry.name);
    addKey(QString("%1 %2").arg(entry.name, entry.variant).trimmed());
    if (entry.name.compare("DarkSpace", Qt::CaseInsensitive) == 0) {
        if (entry.id.contains("d12")) {
            addKey("darkspace-d12");
        } else if (entry.id.contains("d9")) {
            addKey("darkspace-d9");
        }
        addKey("darkspace");
    }
    return keys;
}

QString launchArtworkFolder(const LaunchEntry &entry, bool create)
{
    const QStringList roots = launchArtRoots();
    for (const QString &root : roots) {
        for (const QString &key : launchArtKeys(entry)) {
            const QString candidate = QDir(root).filePath(key);
            if (QFileInfo(candidate).isDir()) {
                return QDir::cleanPath(candidate);
            }
        }
    }

    const QString folder = QDir(roots.isEmpty() ? launchArtRoot() : roots.constFirst()).filePath(primaryLaunchArtKey(entry));
    if (create) {
        QDir().mkpath(folder);
    }
    return QDir::cleanPath(folder);
}

QStringList launchArtworkExtensions()
{
    return {"png", "jpg", "jpeg", "webp", "bmp"};
}

QString findLaunchArtwork(const LaunchEntry &entry, const QStringList &baseNames)
{
    for (const QString &rootPath : launchArtRoots()) {
        const QDir root(rootPath);
        for (const QString &key : launchArtKeys(entry)) {
            const QDir folder(root.filePath(key));
            if (!folder.exists()) {
                continue;
            }
            for (const QString &baseName : baseNames) {
                for (const QString &extension : launchArtworkExtensions()) {
                    const QString candidate = folder.filePath(QString("%1.%2").arg(baseName, extension));
                    if (QFileInfo::exists(candidate)) {
                        return QDir::cleanPath(candidate);
                    }
                }
            }
        }
    }

    return {};
}

QString findLaunchMetadataFile(const LaunchEntry &entry, const QStringList &fileNames)
{
    for (const QString &rootPath : launchArtRoots()) {
        const QDir root(rootPath);
        for (const QString &key : launchArtKeys(entry)) {
            const QDir folder(root.filePath(key));
            if (!folder.exists()) {
                continue;
            }
            for (const QString &fileName : fileNames) {
                const QString candidate = folder.filePath(fileName);
                if (QFileInfo::exists(candidate)) {
                    return QDir::cleanPath(candidate);
                }
            }
        }
    }
    return {};
}

QString launchDescriptionText(const LaunchEntry &entry);

QString launchDescriptionForDisplay(const LaunchEntry &entry)
{
    const QString descriptionPath = findLaunchMetadataFile(entry, {"description.txt", "summary.txt"});
    if (!descriptionPath.isEmpty()) {
        QFile file(descriptionPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString text = QString::fromUtf8(file.read(1800)).trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
    }

    if (!entry.description.trimmed().isEmpty()) {
        return entry.description.trimmed();
    }

    return launchDescriptionText(entry);
}

QString launchDescriptionText(const LaunchEntry &entry)
{
    if (entry.stubOnly) {
        return "Reserved for a future branch once the update path and live infrastructure are ready.";
    }

    const QString key = QString("%1 %2").arg(entry.name, entry.variant).toLower();
    if (key.contains("tutorial")) {
        return "A local DarkSpace tutorial entry for learning the client outside the live server flow.";
    }
    if (key.contains("setup")) {
        return "DarkSpace setup and configuration tools for this installed client branch.";
    }
    if (key.contains("d12")) {
        return "The current x64 community test client, updated from the GameCQ-managed D12 manifest.";
    }
    if (key.contains("d9") || key.contains("original")) {
        return "The original x86 DarkSpace live client, installed through the legacy GameCQ update path.";
    }
    if (key.contains("resourcer")) {
        return "Legacy DarkSpace asset tooling reserved for staff-side workflows.";
    }
    if (entry.customEntry) {
        return "A local game, app, or community tool added to your GameCQ launcher.";
    }
    return "Ready to launch from GameCQ.";
}

QString substitutedCommandLine(const LaunchEntry &entry, const QString &address, int port, quint32 sessionId)
{
    QString commandLine = entry.commandLine;
    commandLine.replace("$DIR", QDir::toNativeSeparators(launchWorkingDirectory(entry, resolveLaunchPath(entry, entry.executable))));
    commandLine.replace("$SID", QString::number(sessionId));
    commandLine.replace("$ADDRESS", address);
    commandLine.replace("$PORT", QString::number(port));
    return commandLine;
}



