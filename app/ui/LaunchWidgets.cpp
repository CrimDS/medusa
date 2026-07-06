#include "ui/LaunchWidgets.h"

#include <QColor>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

LaunchArtLabel::LaunchArtLabel(QWidget *parent)
    : QLabel(parent)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setAlignment(Qt::AlignCenter);
    setWordWrap(true);
}

void LaunchArtLabel::setLaunchArt(const QPixmap &pixmap, const QString &fallbackText)
{
    m_pixmap = pixmap;
    m_fallbackText = fallbackText;
    setText(m_pixmap.isNull() ? fallbackText : QString());
    update();
}

void LaunchArtLabel::paintEvent(QPaintEvent *event)
{
    if (m_pixmap.isNull()) {
        QLabel::paintEvent(event);
        return;
    }

    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    const QRect target = contentsRect().adjusted(2, 2, -2, -2);
    const QPixmap scaled = m_pixmap.scaled(target.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft(
        target.x() + (target.width() - scaled.width()) / 2,
        target.y() + (target.height() - scaled.height()) / 2);
    painter.setClipRect(target);
    painter.drawPixmap(topLeft, scaled);
}

LaunchFeatureFrame::LaunchFeatureFrame(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("launchDetailPanel");
    setAttribute(Qt::WA_StyledBackground, true);
}

void LaunchFeatureFrame::setBackgroundArt(const QPixmap &pixmap)
{
    m_background = pixmap;
    update();
}

void LaunchFeatureFrame::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    if (!m_background.isNull()) {
        const QRect target = rect().adjusted(1, 1, -1, -1);
        const QPixmap scaled = m_background.scaled(target.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const QPoint topLeft(
            target.x() + (target.width() - scaled.width()) / 2,
            target.y() + (target.height() - scaled.height()) / 2);
        painter.setClipRect(target);
        painter.drawPixmap(topLeft, scaled);
        painter.fillRect(target, QColor(6, 8, 12, 142));
    }

    event->accept();
}
