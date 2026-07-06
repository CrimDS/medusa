#pragma once

#include <QString>

namespace AppPaths {

// Portable mode is enabled by placing portable-settings.ini beside GameCQ.exe.
// Release packages include that marker so settings, logs, and user data stay
// near the executable instead of leaking into roaming profile state.
bool portableModeEnabled();
void configurePortableSettings();
QString localDataRoot(const QString &folderName, const QString &fallbackRoot = {});

}
