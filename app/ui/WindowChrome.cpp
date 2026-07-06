#include "ui/WindowChrome.h"

#include <QColor>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QSize>
#include <QToolButton>
#include <QWindow>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace {

constexpr int kResizeGripWidth = 9;

} // namespace

WindowChromeBar::WindowChromeBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("windowChrome");
}

void WindowChromeBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (QWindow *handle = window()->windowHandle()) {
            handle->startSystemMove();
            event->accept();
            return;
        }
    }
    QFrame::mousePressEvent(event);
}

void WindowChromeBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QWidget *topLevel = window();
        topLevel->isMaximized() ? topLevel->showNormal() : topLevel->showMaximized();
        event->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}

QIcon makeWindowControlIcon(WindowControlIcon icon)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#c8d2e2"), 1.6, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (icon) {
    case WindowControlIcon::Minimize:
        painter.drawLine(QPointF(4, 11), QPointF(12, 11));
        break;
    case WindowControlIcon::Maximize:
        painter.drawRect(QRectF(4.5, 4.5, 7, 7));
        break;
    case WindowControlIcon::Restore:
        painter.drawRect(QRectF(3.5, 6.5, 6, 6));
        painter.drawPolyline(QPolygonF{
            QPointF(6.5, 4.5),
            QPointF(12.5, 4.5),
            QPointF(12.5, 10.5),
            QPointF(10.5, 10.5),
        });
        break;
    case WindowControlIcon::Close:
        painter.drawLine(QPointF(5, 5), QPointF(11, 11));
        painter.drawLine(QPointF(11, 5), QPointF(5, 11));
        break;
    }

    return QIcon(pixmap);
}

QToolButton *makeWindowControlButton(WindowControlIcon icon, const QString &toolTip, const QString &objectName)
{
    auto *button = new QToolButton;
    button->setObjectName(objectName);
    button->setIcon(makeWindowControlIcon(icon));
    button->setIconSize(QSize(16, 16));
    button->setToolTip(toolTip);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::ArrowCursor);
    return button;
}

void enableWindowsFramelessResize(QWidget *window)
{
#ifdef Q_OS_WIN
    if (!window) {
        return;
    }

    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return;
    }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
#else
    Q_UNUSED(window);
#endif
}

bool handleWindowsFramelessResizeNativeEvent(QWidget *window, const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);
    if (!window || !message || !result || window->isMaximized() || window->isFullScreen()) {
        return false;
    }

    MSG *msg = static_cast<MSG *>(message);
    if (msg->message != WM_NCHITTEST) {
        return false;
    }

    const QPoint globalPos(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));
    const QPoint localPos = window->mapFromGlobal(globalPos);
    const int border = kResizeGripWidth + 3;
    const bool left = localPos.x() >= -border && localPos.x() < border;
    const bool right = localPos.x() <= window->width() + border && localPos.x() > window->width() - border;
    const bool top = localPos.y() >= -border && localPos.y() < border;
    const bool bottom = localPos.y() <= window->height() + border && localPos.y() > window->height() - border;

    if (top && left) {
        *result = HTTOPLEFT;
        return true;
    }
    if (top && right) {
        *result = HTTOPRIGHT;
        return true;
    }
    if (bottom && left) {
        *result = HTBOTTOMLEFT;
        return true;
    }
    if (bottom && right) {
        *result = HTBOTTOMRIGHT;
        return true;
    }
    if (left) {
        *result = HTLEFT;
        return true;
    }
    if (right) {
        *result = HTRIGHT;
        return true;
    }
    if (top) {
        *result = HTTOP;
        return true;
    }
    if (bottom) {
        *result = HTBOTTOM;
        return true;
    }
#else
    Q_UNUSED(window);
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif

    return false;
}
