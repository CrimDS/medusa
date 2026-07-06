#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {

constexpr auto kPortableSettingsMarker = "portable-settings.ini";

QString applicationDirFile(const QString &path)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(path);
}

QString portableUserDataRoot()
{
    return applicationDirFile("UserData");
}

} // namespace

namespace AppPaths {

bool portableModeEnabled()
{
    return QFileInfo::exists(applicationDirFile(kPortableSettingsMarker));
}

void configurePortableSettings()
{
    if (!portableModeEnabled()) {
        return;
    }

    const QString userDataRoot = portableUserDataRoot();
    QDir().mkpath(userDataRoot);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, userDataRoot);
}

QString localDataRoot(const QString &folderName, const QString &fallbackRoot)
{
    if (portableModeEnabled()) {
        const QString root = QDir(portableUserDataRoot()).filePath(folderName);
        QDir().mkpath(root);
        return QDir::cleanPath(root);
    }

    QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        root = fallbackRoot.isEmpty() ? applicationDirFile(QString(".Cache/%1").arg(folderName)) : fallbackRoot;
    }
    QDir().mkpath(root);
    return QDir::cleanPath(root);
}

}
