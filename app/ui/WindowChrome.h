#pragma once

#include <QByteArray>
#include <QFrame>
#include <QIcon>
#include <QtGlobal>

class QMouseEvent;
class QString;
class QToolButton;
class QWidget;

class WindowChromeBar final : public QFrame {
public:
    explicit WindowChromeBar(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
};

enum class WindowControlIcon {
    Minimize,
    Maximize,
    Restore,
    Close,
};

QIcon makeWindowControlIcon(WindowControlIcon icon);
QToolButton *makeWindowControlButton(WindowControlIcon icon, const QString &toolTip, const QString &objectName);
void enableWindowsFramelessResize(QWidget *window);
bool handleWindowsFramelessResizeNativeEvent(QWidget *window, const QByteArray &eventType, void *message, qintptr *result);
