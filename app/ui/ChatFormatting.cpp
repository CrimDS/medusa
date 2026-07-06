#include "ui/ChatFormatting.h"

#include <QColor>
#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>
#include <QTextDocument>
#include <QTime>
#include <QUrl>
QString chatTime(quint32 seconds)
{
    if (seconds == 0) {
        return QTime::currentTime().toString("HH:mm");
    }

    return QDateTime::fromSecsSinceEpoch(seconds).toLocalTime().time().toString("HH:mm");
}


int legacyChatColorCheck(const QColor &color)
{
    const unsigned int packed = (static_cast<unsigned int>(color.red()) << 16)
        | (static_cast<unsigned int>(color.green()) << 8)
        | static_cast<unsigned int>(color.blue());
    const unsigned int red = packed & 0xff;
    const unsigned int green = (packed & 0xff00) >> 8;
    const unsigned int blue = (packed & 0xff0000) >> 16;

    if ((red + green + blue > 0x70) && (red > 0x60 || green > 0x60 || blue > 0x80)) {
        if (red + green + blue < 624 || red < 0xb0 || green < 0xb0 || blue < 0xb0) {
            return 0;
        }
        return 1;
    }

    return -1;
}

QString legacyOutgoingChatColor(const QString &colorText)
{
    const QColor color(colorText);
    if (!color.isValid()) {
        return {};
    }

    if (legacyChatColorCheck(color) != 0) {
        return "00ffff";
    }

    return color.name(QColor::HexRgb).mid(1).toLower();
}

QString normalizedOutgoingChatText(QString text)
{
    text.replace('\r', ' ');
    text.replace('\n', ' ');

    text.remove(QRegularExpression("</font>", QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression("<(/)?(b|i)>", QRegularExpression::CaseInsensitiveOption));
    text.remove(QRegularExpression("<font\\s+color=[0-9a-fA-F]{6}>", QRegularExpression::CaseInsensitiveOption));

    return text;
}

QString richTextToPlainText(const QString &html)
{
    QTextDocument document;
    document.setHtml(html);
    return document.toPlainText();
}

QString legacyChatMarkupToPlainText(const QString &text)
{
    return richTextToPlainText(legacyChatMarkupToHtml(text));
}

QString logTime(quint32 seconds)
{
    const QDateTime time = seconds == 0 ? QDateTime::currentDateTime() : QDateTime::fromSecsSinceEpoch(seconds).toLocalTime();
    return time.toString("yyyy-MM-dd HH:mm:ss");
}

QString chatNameClass(quint32 authorId)
{
    return (authorId % 2) == 0 ? "author-primary" : "author-success";
}

