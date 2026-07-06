#include "ui/MainWindow.h"

#include "net/MetaClientBridge.h"
#include "ui/ChatFormatting.h"
#include "ui/ThemeManager.h"

#include <QColor>
#include <QColorDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>

void MainWindow::setChatCommand(const QString &command)
{
    if (!m_chatInput) {
        return;
    }

    m_chatInput->setText(command);
    m_chatInput->setFocus();
    m_chatInput->setCursorPosition(m_chatInput->text().size());
}


void MainWindow::postLocalChatLine()
{
    if (!m_chatInput || m_chatInput->text().trimmed().isEmpty()) {
        return;
    }

    QString message = normalizedOutgoingChatText(m_chatInput->text());
    if (!message.trimmed().startsWith('/')) {
        const QString color = legacyOutgoingChatColor(m_chatColor);
        if (!color.isEmpty()) {
            message = QString("<font color=%1>%2</font>").arg(color, message);
        }
    }

    m_bridge->sendChatMessage(message);
    m_chatInput->clear();
}

void MainWindow::chooseOutgoingChatColor()
{
    const QColor initial(m_chatColor.isEmpty() ? QString("#e8ebf0") : m_chatColor);
    const QColor color = QColorDialog::getColor(initial, this, "Outgoing Text Color");
    if (!color.isValid()) {
        return;
    }

    m_chatColor = color.name(QColor::HexRgb).toUpper();
    QSettings().setValue("Chat/OutgoingColor", m_chatColor);
    updateChatColorButton();
    statusBar()->showMessage(QString("Chat color set to %1.").arg(m_chatColor), 3000);
}

void MainWindow::updateChatColorButton()
{
    if (!m_chatColorButton) {
        return;
    }

    const QColor color(m_chatColor);
    if (!color.isValid()) {
        m_chatColorButton->setStyleSheet({});
        m_chatColorButton->setToolTip("Outgoing text color");
        return;
    }

    const QString glyphColor = color.name(QColor::HexRgb);
    const QString buttonBg = ThemeManager::colorName("toolbarBg");
    const QString hoverBg = ThemeManager::colorName("hover");
    const QString border = ThemeManager::colorName("border");
    m_chatColorButton->setToolTip(QString("Outgoing text color: %1").arg(glyphColor.toUpper()));
    m_chatColorButton->setStyleSheet(QString(
        "QPushButton#chatTextButton {"
        "background:%2;"
        "color:%1;"
        "border:1px solid %4;"
        "border-radius:8px;"
        "min-width:36px;max-width:36px;min-height:36px;max-height:36px;"
        "padding:0;font-weight:800;"
        "}"
        "QPushButton#chatTextButton:hover {"
        "background:%3;"
        "border-color:%1;"
        "}")
        .arg(glyphColor, buttonBg, hoverBg, border));
}


