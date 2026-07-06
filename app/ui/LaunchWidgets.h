#pragma once

#include <QFrame>
#include <QLabel>
#include <QPixmap>

class QPaintEvent;

class LaunchArtLabel final : public QLabel {
public:
    explicit LaunchArtLabel(QWidget *parent = nullptr);

    void setLaunchArt(const QPixmap &pixmap, const QString &fallbackText);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap m_pixmap;
    QString m_fallbackText;
};

class LaunchFeatureFrame final : public QFrame {
public:
    explicit LaunchFeatureFrame(QWidget *parent = nullptr);

    void setBackgroundArt(const QPixmap &pixmap);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPixmap m_background;
};
