#pragma once

#include <QElapsedTimer>
#include <QLabel>
#include <QWidget>

class StatusLine final : public QWidget {
    Q_OBJECT

public:
    explicit StatusLine(QWidget *parent = nullptr);

public slots:
    void setConnectionState(const QString &state);
    void setProfile(const QString &displayName, quint32 flags);

private slots:
    void updateOnlineTime();

private:
    QLabel *m_nameLabel = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_modFlag = nullptr;
    QLabel *m_devFlag = nullptr;
    QLabel *m_freeFlag = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    QElapsedTimer m_elapsed;
};
