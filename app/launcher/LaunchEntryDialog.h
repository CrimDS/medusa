#pragma once

#include "launcher/LaunchCatalog.h"

#include <QString>

class QWidget;

bool editCustomLaunchEntry(QWidget *parent, LaunchEntry &entry, bool isNew, QString *bannerArtwork, QString *capsuleArtwork);
