#include "ui/StatusLine.h"

#include <QHBoxLayout>
#include <QStyle>
#include <QTimer>

namespace {

QLabel *makeLabel(const QString &text, const QString &objectName = QString())
{
    auto *label = new QLabel(text);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    return label;
}

QLabel *makeFlag(const QString &text, const QString &kind)
{
    auto *label = makeLabel(text, "statusFlag");
    label->setProperty("kind", kind);
    return label;
}

} // namespace

StatusLine::StatusLine(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->setSpacing(8);

    layout->addWidget(makeLabel("Logged in as"));
    m_nameLabel = makeLabel("Not logged in", "statusName");
    layout->addWidget(m_nameLabel);
    layout->addWidget(makeLabel("Status"));
    m_stateLabel = makeLabel("Offline", "statusOnline");
    layout->addWidget(m_stateLabel);
    m_modFlag = makeFlag("MOD", "mod");
    m_devFlag = makeFlag("DEV", "dev");
    m_freeFlag = makeFlag("FREE", "free");
    layout->addWidget(m_modFlag);
    layout->addWidget(m_devFlag);
    layout->addWidget(m_freeFlag);
    layout->addWidget(makeLabel("Online:"));

    m_elapsedLabel = makeLabel("00:00:00", "statusElapsed");
    layout->addWidget(m_elapsedLabel);

    m_elapsed.start();

    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &StatusLine::updateOnlineTime);
    timer->start(1000);

    setProfile(QString(), 0);
    setConnectionState("Offline");
}

void StatusLine::setConnectionState(const QString &state)
{
    m_stateLabel->setText(state);
    m_stateLabel->setProperty("online", state.compare("Online", Qt::CaseInsensitive) == 0);
    m_stateLabel->style()->unpolish(m_stateLabel);
    m_stateLabel->style()->polish(m_stateLabel);
}

void StatusLine::setProfile(const QString &displayName, quint32 flags)
{
    constexpr quint32 moderator = 0x00000004;
    constexpr quint32 subscribed = 0x00000040;
    constexpr quint32 developer = 0x00000800;

    m_nameLabel->setText(displayName.isEmpty() ? "Not logged in" : displayName);
    m_modFlag->setVisible((flags & moderator) != 0);
    m_devFlag->setVisible((flags & developer) != 0);
    m_freeFlag->setVisible((flags & subscribed) == 0 && !displayName.isEmpty());
}

void StatusLine::updateOnlineTime()
{
    const qint64 seconds = m_elapsed.elapsed() / 1000;
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds / 60) % 60;
    const qint64 secs = seconds % 60;

    m_elapsedLabel->setText(QString("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(secs, 2, 10, QLatin1Char('0')));
}
