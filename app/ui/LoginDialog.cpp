#include "ui/LoginDialog.h"
#include "ui/LegacyIcons.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>


LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName("loginDialog");
    setWindowTitle("Login");
    setWindowIcon(legacyIcon("GCQL"));
    setModal(true);
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 20, 22, 18);
    layout->setSpacing(14);

    auto *brand = new QLabel("GameCQ <span>1 ⅜</span>");
    brand->setObjectName("loginBrand");
    brand->setTextFormat(Qt::RichText);
    layout->addWidget(brand, 0, Qt::AlignHCenter);

    auto *subtitle = new QLabel("Darkspace lobby client");
    subtitle->setObjectName("loginSubtitle");
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

    auto *panel = new QFrame(this);
    panel->setObjectName("loginPanel");
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(14, 14, 14, 14);
    panelLayout->setSpacing(10);

    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignTop);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);

    m_account = new QLineEdit;
    m_account->setPlaceholderText("Account name");
    form->addRow("Account", m_account);

    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText("Password");
    connect(m_password, &QLineEdit::returnPressed, this, &LoginDialog::submit);
    form->addRow("Password", m_password);

    m_address = new QLineEdit("meta-server.palestar.com");
    form->addRow("Meta-server", m_address);

    m_port = new QSpinBox;
    m_port->setRange(1, 65535);
    m_port->setValue(9000);
    form->addRow("Port", m_port);

    panelLayout->addLayout(form);

    m_remember = new QCheckBox("Remember account name");
    panelLayout->addWidget(m_remember);

    m_rememberPassword = new QCheckBox("Remember password securely");
    panelLayout->addWidget(m_rememberPassword);

    m_autoLogin = new QCheckBox("Automatically log in");
    panelLayout->addWidget(m_autoLogin);

    layout->addWidget(panel);

    connect(m_rememberPassword, &QCheckBox::toggled, this, [this](bool checked) {
        m_autoLogin->setEnabled(checked);
        if (!checked) {
            m_autoLogin->setChecked(false);
        }
        if (checked) {
            m_remember->setChecked(true);
        }
    });
    connect(m_autoLogin, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_remember->setChecked(true);
            m_rememberPassword->setChecked(true);
        }
    });
    m_autoLogin->setEnabled(false);

    m_message = new QLabel("Ready to connect.");
    m_message->setObjectName("loginMessage");
    m_message->setWordWrap(true);
    layout->addWidget(m_message);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);

    auto *cancel = new QPushButton("Cancel");
    connect(cancel, &QPushButton::clicked, this, &LoginDialog::reject);
    buttons->addWidget(cancel);

    m_loginButton = new QPushButton("Login");
    m_loginButton->setObjectName("primaryButton");
    connect(m_loginButton, &QPushButton::clicked, this, &LoginDialog::submit);
    buttons->addWidget(m_loginButton);

    layout->addLayout(buttons);
}

QString LoginDialog::accountName() const
{
    return m_account->text().trimmed();
}

bool LoginDialog::rememberAccountName() const
{
    return m_remember->isChecked();
}

bool LoginDialog::rememberPassword() const
{
    return m_rememberPassword->isChecked();
}

bool LoginDialog::autoLogin() const
{
    return m_autoLogin->isChecked();
}

void LoginDialog::setAccountName(const QString &account)
{
    m_account->setText(account);
}

void LoginDialog::setPassword(const QString &password)
{
    m_password->setText(password);
}

void LoginDialog::setRememberAccountName(bool remember)
{
    m_remember->setChecked(remember);
}

void LoginDialog::setRememberPassword(bool remember)
{
    m_rememberPassword->setChecked(remember);
    m_autoLogin->setEnabled(remember);
    if (remember) {
        m_remember->setChecked(true);
    }
}

void LoginDialog::setAutoLogin(bool enabled)
{
    m_autoLogin->setChecked(enabled);
    if (enabled) {
        m_remember->setChecked(true);
        m_rememberPassword->setChecked(true);
    }
}

void LoginDialog::setConnectionDefaults(const QString &address, int port)
{
    if (!address.trimmed().isEmpty()) {
        m_address->setText(address.trimmed());
    }
    if (port >= m_port->minimum() && port <= m_port->maximum()) {
        m_port->setValue(port);
    }
}

void LoginDialog::setBusy(bool busy, const QString &message, bool error)
{
    m_loginButton->setEnabled(!busy);
    m_account->setEnabled(!busy);
    m_password->setEnabled(!busy);
    m_address->setEnabled(!busy);
    m_port->setEnabled(!busy);
    m_remember->setEnabled(!busy);
    m_rememberPassword->setEnabled(!busy);
    m_autoLogin->setEnabled(!busy && m_rememberPassword->isChecked());

    if (!message.isEmpty()) {
        m_message->setText(message);
        m_message->setProperty("error", error);
        m_message->style()->unpolish(m_message);
        m_message->style()->polish(m_message);
    }
}

void LoginDialog::setStatus(const QString &message)
{
    if (!message.isEmpty()) {
        m_message->setText(message + "...");
        m_message->setProperty("error", false);
        m_message->style()->unpolish(m_message);
        m_message->style()->polish(m_message);
    }
}

void LoginDialog::submit()
{
    if (m_account->text().trimmed().isEmpty()) {
        setBusy(false, "Enter an account name.", true);
        return;
    }

    if (m_password->text().isEmpty()) {
        setBusy(false, "Enter a password.", true);
        return;
    }

    setBusy(true, "Connecting...");
    emit loginRequested(m_address->text().trimmed(), m_port->value(), m_account->text().trimmed(), m_password->text());
}
