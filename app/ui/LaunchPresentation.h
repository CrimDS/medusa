#pragma once

#include "launcher/LaunchCatalog.h"

#include <QIcon>
#include <QString>

class QFrame;
class QLabel;

QIcon launchProgramIcon(const LaunchEntry &entry);
QIcon launchListIcon(const QIcon &icon);
void setLaunchFeatureBackground(QFrame *frame, const LaunchEntry &entry);
void setLaunchCapsuleArtwork(QLabel *label, const LaunchEntry &entry);
QString launchStatus(const LaunchEntry &entry);
QString launchStatusPillName(const QString &status);
bool isPrimaryDarkSpaceLaunchEntry(const LaunchEntry &entry);
bool shouldShowLaunchStatusText(const LaunchEntry &entry, const QString &status);