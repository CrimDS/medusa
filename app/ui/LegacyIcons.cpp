#include "ui/LegacyIcons.h"

#include <QLatin1String>
#include <QString>

QIcon legacyIcon(const char *name)
{
    return QIcon(QString(":/gamecq/icons/%1.ico").arg(QLatin1String(name)));
}
