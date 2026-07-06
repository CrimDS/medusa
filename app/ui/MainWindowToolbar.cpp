#include "ui/MainWindow.h"

#include "ui/LegacyIcons.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRadialGradient>
#include <QSize>
#include <QToolButton>

namespace {

QFrame *makePanel(const QString &objectName)
{
    auto *panel = new QFrame;
    panel->setObjectName(objectName);
    panel->setFrameShape(QFrame::NoFrame);
    return panel;
}

QIcon legacyToolIconForText(const QString &text)
{
    const QString key = text.toLower();
    if (key.contains("message")) {
        return legacyIcon("friend");
    }
    if (key.contains("friend")) {
        return legacyIcon("friends");
    }
    if (key.contains("fleet") || key.contains("clan")) {
        return legacyIcon("clans");
    }
    if (key.contains("room")) {
        return legacyIcon("rooms");
    }
    if (key.contains("profile") || key.contains("user") || key.contains("staff") || key.contains("find") || key.contains("clone")) {
        return legacyIcon("Avatar");
    }
    if (key.contains("connect")) {
        return legacyIcon("game");
    }
    if (key.contains("launch") || key.contains("play") || key.contains("game") || key.contains("darkspace") || key.contains("server") || key.contains("open")) {
        return legacyIcon("games");
    }
    if (key.contains("emote")) {
        return legacyIcon("activity");
    }
    if (key.contains("create") || key.contains("add")) {
        return legacyIcon("game");
    }
    if (key.contains("install") || key.contains("download")) {
        return legacyIcon("down");
    }
    if (key.contains("repair") || key.contains("option") || key.contains("filter") || key.contains("process") || key.contains("profiler") || key.contains("watch") || key.contains("check") || key.contains("default") || key.contains("manage")) {
        return legacyIcon("tool");
    }
    if (key.contains("delete") || key.contains("remove")) {
        return legacyIcon("ico00002");
    }
    if (key.contains("cancel") || key.contains("ignore") || key.contains("close") || key.contains("mute") || key.contains("kick") || key.contains("ban")) {
        return legacyIcon("cancel");
    }
    if (key.contains("refresh")) {
        return legacyIcon("activity");
    }
    if (key.contains("forward")) {
        return legacyIcon("right");
    }
    if (key.contains("back")) {
        return legacyIcon("up");
    }
    if (key.contains("home") || key.contains("news") || key.contains("forum") || key.contains("manual") || key.contains("command") || key.contains("folder")) {
        return legacyIcon("GCQLDoc");
    }
    return {};
}

} // namespace

QFrame *MainWindow::createToolbar()
{
    auto *toolbar = makePanel("contextToolbar");
    auto *layout = new QHBoxLayout(toolbar);
    layout->setContentsMargins(8, 3, 8, 3);
    layout->setSpacing(2);
    return toolbar;
}

QToolButton *MainWindow::createToolButton(const QString &text, const QColor &color)
{
    auto *button = new QToolButton;
    button->setText(text);
    const QIcon icon = legacyToolIconForText(text);
    button->setIcon(icon.isNull() ? makeDiamondIcon(color) : icon);
    button->setIconSize(icon.isNull() ? QSize(14, 14) : QSize(16, 16));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(false);
    return button;
}

QIcon MainWindow::makeDiamondIcon(const QColor &color, bool active) const
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (active) {
        QRadialGradient glow(QPointF(12, 12), 11);
        QColor center = color;
        center.setAlpha(120);
        QColor edge = color;
        edge.setAlpha(0);
        glow.setColorAt(0.0, center);
        glow.setColorAt(1.0, edge);
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(QRectF(1, 1, 22, 22));
    }

    painter.translate(12, 12);
    painter.rotate(45);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(-5, -5, 10, 10), 2, 2);

    return QIcon(pixmap);
}
