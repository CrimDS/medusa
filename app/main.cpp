#include "core/AppPaths.h"
#include "ui/MainWindow.h"
#include "ui/ThemeManager.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("GameCQ");
    QApplication::setApplicationVersion("1.3.8");
    QApplication::setOrganizationName("Palestar");
    AppPaths::configurePortableSettings();

    ThemeManager::applyTheme(app);
    app.setWindowIcon(QIcon(":/gamecq/icons/GCQL.ico"));

    MainWindow window;
    window.resize(1180, 760);
    window.show();

    return app.exec();
}
