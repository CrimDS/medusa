#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QSettings>
#include <QString>

namespace gamecq::chat_ui {

constexpr int kMaxChatDisplayLines = 1500;

struct DiscordRelaySettings {
    bool text = true;
    bool images = true;
    bool gifs = true;
};

inline DiscordRelaySettings discordRelaySettings()
{
    QSettings settings;
    return {
        settings.value("Chat/DiscordTextRelayEnabled", true).toBool(),
        settings.value("Chat/DiscordImageRelayEnabled", true).toBool(),
        settings.value("Chat/DiscordGifRelayEnabled", true).toBool(),
    };
}

inline QIcon makeSendIcon(const QColor &color)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(QPolygonF{
        QPointF(7.0, 5.0),
        QPointF(11.2, 9.0),
        QPointF(7.0, 13.0),
    });

    return QIcon(pixmap);
}

} // namespace gamecq::chat_ui
