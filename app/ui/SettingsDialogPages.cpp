#include "ui/SettingsDialog.h"

#include "core/AppPaths.h"

#include <QCheckBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QWidget>

namespace {

constexpr auto kDefaultMetaServer = "meta-server.palestar.com";
constexpr int kDefaultMetaPort = 9000;

QLineEdit *makeLineEdit(const QString &placeholder = {})
{
    auto *field = new QLineEdit;
    field->setPlaceholderText(placeholder);
    return field;
}

QString defaultChatLogFolder()
{
    const QString root = AppPaths::localDataRoot("GameCQ", QDir::homePath() + "/GameCQ");
    return QDir(root).filePath("ChatLogs");
}

} // namespace

QWidget *SettingsDialog::createAccountPage(QWidget *parent)
{
    auto *accountPage = new QWidget(parent);
    auto *accountLayout = new QFormLayout(accountPage);
    accountLayout->setHorizontalSpacing(12);
    accountLayout->setVerticalSpacing(10);

    m_rememberAccount = new QCheckBox("Remember account name", accountPage);
    accountLayout->addRow({}, m_rememberAccount);
    m_rememberPassword = new QCheckBox("Remember password securely", accountPage);
    accountLayout->addRow({}, m_rememberPassword);
    m_autoLogin = new QCheckBox("Automatically log in", accountPage);
    accountLayout->addRow({}, m_autoLogin);

    connect(m_rememberPassword, &QCheckBox::toggled, this, [this](bool checked) {
        m_autoLogin->setEnabled(checked);
        if (!checked) {
            m_autoLogin->setChecked(false);
        }
        if (checked) {
            m_rememberAccount->setChecked(true);
        }
    });
    connect(m_autoLogin, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_rememberAccount->setChecked(true);
            m_rememberPassword->setChecked(true);
        }
    });

    m_accountName = makeLineEdit("Account name");
    accountLayout->addRow("Account", m_accountName);
    m_metaServer = makeLineEdit(kDefaultMetaServer);
    accountLayout->addRow("Meta-server", m_metaServer);
    m_metaPort = new QSpinBox(accountPage);
    m_metaPort->setRange(1, 65535);
    accountLayout->addRow("Port", m_metaPort);

    return accountPage;
}

QWidget *SettingsDialog::createChatPage(QWidget *parent)
{
    auto *chatPage = new QWidget(parent);
    auto *chatLayout = new QFormLayout(chatPage);
    chatLayout->setHorizontalSpacing(12);
    chatLayout->setVerticalSpacing(10);

    m_chatLogging = new QCheckBox("Save chat logs", chatPage);
    chatLayout->addRow({}, m_chatLogging);
    m_loadChatHistory = new QCheckBox("Load previous chat log on login", chatPage);
    chatLayout->addRow({}, m_loadChatHistory);

    auto *logFolderRow = new QHBoxLayout;
    m_chatLogFolder = makeLineEdit(defaultChatLogFolder());
    m_chatLogFolderButton = new QPushButton("Browse", chatPage);
    connect(m_chatLogFolderButton, &QPushButton::clicked, this, &SettingsDialog::chooseChatLogFolder);
    logFolderRow->addWidget(m_chatLogFolder, 1);
    logFolderRow->addWidget(m_chatLogFolderButton);
    chatLayout->addRow("Log folder", logFolderRow);

    connect(m_chatLogging, &QCheckBox::toggled, this, [this](bool checked) {
        m_chatLogFolder->setEnabled(checked);
        m_chatLogFolderButton->setEnabled(checked);
        m_loadChatHistory->setEnabled(checked);
        if (!checked) {
            m_loadChatHistory->setChecked(false);
        }
    });

    m_discordTextRelay = new QCheckBox("Relay Discord text messages", chatPage);
    chatLayout->addRow({}, m_discordTextRelay);
    m_discordImageRelay = new QCheckBox("Relay Discord image and sticker messages", chatPage);
    chatLayout->addRow({}, m_discordImageRelay);
    m_discordGifRelay = new QCheckBox("Relay Discord GIF messages", chatPage);
    chatLayout->addRow({}, m_discordGifRelay);

    return chatPage;
}
