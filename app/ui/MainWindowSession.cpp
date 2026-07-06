#include "ui/MainWindow.h"

#include "launcher/LaunchCatalog.h"
#include "security/CredentialStore.h"
#include "ui/ChatLogWriter.h"
#include "ui/HelpDialogs.h"
#include "ui/LoginDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/ThemeManager.h"
#include "net/MetaClientBridge.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QStatusBar>

namespace {

QString settingString(const QString &key, const QString &fallback = {})
{
    return QSettings().value(key, fallback).toString();
}

QString suggestedAccountName(QString name)
{
    name = name.trimmed();
    name.remove(QRegularExpression("\\s*\\([^)]*\\)\\s*$"));
    name = name.trimmed();

    // The lobby profile name often includes clan tags and temporary status
    // text. The account rename call must receive only the base account name.
    static const QRegularExpression leadingClanTags("^\\s*(\\[[^\\]]+\\]\\s*)+");
    const QRegularExpressionMatch match = leadingClanTags.match(name);
    if (match.hasMatch()) {
        name = name.mid(match.capturedEnd()).trimmed();
    }

    return name;
}

QString accountNameValidationError(const QString &name)
{
    // Validate locally before sending the change-name request. The legacy
    // server reports many failures as a generic illegal-name error, so client
    // side checks give the user a specific fix without relaxing server rules.
    if (name.size() < 3) {
        return "Name must be at least 3 characters.";
    }
    if (name.size() > 35) {
        return "Name must be 35 characters or less.";
    }
    if (name.front().isSpace() || name.back().isSpace()) {
        return "Name cannot start or end with a space.";
    }
    if (name.front() == QLatin1Char('@')) {
        return "Names cannot start with @.";
    }
    if (name.contains('%')) {
        return "Names cannot contain %.";
    }
    if (name.contains('<') || name.contains('>')) {
        return "Names cannot contain < or >.";
    }
    if (name.contains('[') || name.contains(']')) {
        return "Names cannot contain clan tags or square brackets.";
    }

    const QString lower = name.toLower();
    if (lower.contains("[b]") || lower.contains("[/b]") || lower.contains("[i]") || lower.contains("[/i]")
        || lower.contains("[color=") || lower.contains("[/color]")) {
        return "Names cannot contain chat formatting tags.";
    }
    if (lower.contains("&#91") || lower.contains("&#93") || lower.contains("&nbsp") || lower.contains("&#32")) {
        return "Names cannot contain escaped brackets or spaces.";
    }

    int letters = 0;
    for (const QChar ch : name) {
        if (!ch.isPrint()) {
            return "Names can only contain printable characters.";
        }
        if (ch.isLetter()) {
            ++letters;
        }
    }
    if (letters < 2) {
        return "Name must contain at least 2 letters.";
    }

    static const QStringList reservedAnywhere = {"darkspace", "palestar", "admin", "transport"};
    for (const QString &reserved : reservedAnywhere) {
        if (lower.contains(reserved)) {
            return QString("Names cannot contain \"%1\".").arg(reserved);
        }
    }

    static const QStringList reservedPrefixes = {"everyone", "anyone", "you", "server"};
    for (const QString &reserved : reservedPrefixes) {
        if (lower.startsWith(reserved)) {
            return QString("Names cannot start with \"%1\".").arg(reserved);
        }
    }

    return {};
}

} // namespace
void MainWindow::applySettings()
{
    m_chatLogWriter->resetWriteFailure();
    m_chatColor = settingString("Chat/OutgoingColor").trimmed();
    if (!m_chatColor.startsWith('#') && !m_chatColor.isEmpty()) {
        m_chatColor.prepend('#');
    }
    if (!QColor(m_chatColor).isValid()) {
        m_chatColor.clear();
    }
    applyThemeAppearance();

    ensureInstallLayout();
    populateLaunchList();
    refreshLaunchStatus();
}

void MainWindow::ensureInstallLayout()
{
    QDir appDir(QCoreApplication::applicationDirPath());
    appDir.mkpath(".Cache");
    appDir.mkpath(".Cache/DarkSpace");
    appDir.mkpath(".Cache/DarkSpaceBeta");
    appDir.mkpath(".Cache/Resourcer");
    appDir.mkpath("DarkSpace");
    QDir().mkpath(d12InstallRoot());
}

bool MainWindow::tryAutoLogin()
{
    if (m_autoLoginAttempted || m_sessionId != 0) {
        return false;
    }

    QSettings settings;
    if (!settings.value("Account/AutoLogin", false).toBool() || !settings.value("Account/RememberPassword", false).toBool()) {
        return false;
    }

    const QString account = settings.value("Account/Name").toString().trimmed();
    const QString address = settings.value("Account/MetaServer", "meta-server.palestar.com").toString().trimmed();
    const int port = settings.value("Account/MetaPort", 9000).toInt();
    if (account.isEmpty() || address.isEmpty()) {
        return false;
    }

    QString password;
    QString error;
    if (!CredentialStore::readPassword(address, port, account, &password, &error)) {
        if (!error.isEmpty()) {
            statusBar()->showMessage(error, 6000);
        }
        return false;
    }

    // Reuse the normal pending-login path so successful auto-login persists the
    // same settings as a manual login and failed auto-login leaves no partial
    // password state behind.
    m_autoLoginAttempted = true;
    m_autoLoginInProgress = true;
    m_pendingLoginAddress = address;
    m_pendingLoginPort = port;
    m_pendingLoginAccount = account;
    m_pendingLoginPassword = password;
    m_pendingRememberName = true;
    m_pendingRememberPassword = true;
    m_pendingAutoLogin = true;

    clearProfile();
    statusBar()->showMessage(QString("Auto logging in as %1...").arg(account), 5000);
    m_bridge->connectAndLogin(address, port, account, password);
    return true;
}

void MainWindow::clearPendingLogin()
{
    m_pendingLoginAddress.clear();
    m_pendingLoginAccount.clear();
    m_pendingLoginPassword.clear();
    m_pendingLoginPort = 0;
    m_pendingRememberName = false;
    m_pendingRememberPassword = false;
    m_pendingAutoLogin = false;
    m_autoLoginInProgress = false;
}

void MainWindow::persistPendingLogin()
{
    if (m_pendingLoginAddress.isEmpty() || m_pendingLoginAccount.isEmpty()) {
        clearPendingLogin();
        return;
    }

    QSettings settings;
    settings.setValue("Account/MetaServer", m_pendingLoginAddress);
    settings.setValue("Account/MetaPort", m_pendingLoginPort);
    if (m_pendingRememberName) {
        settings.setValue("Account/RememberName", true);
        settings.setValue("Account/Name", m_pendingLoginAccount);
    } else {
        settings.setValue("Account/RememberName", false);
        settings.remove("Account/Name");
    }

    if (m_pendingRememberPassword) {
        // Passwords never go into QSettings. QSettings only stores whether the
        // account should be remembered; the secret itself lives in the OS vault.
        QString error;
        if (CredentialStore::writePassword(m_pendingLoginAddress, m_pendingLoginPort, m_pendingLoginAccount, m_pendingLoginPassword, &error)) {
            settings.setValue("Account/RememberPassword", true);
            settings.setValue("Account/AutoLogin", m_pendingAutoLogin);
        } else {
            settings.setValue("Account/RememberPassword", false);
            settings.setValue("Account/AutoLogin", false);
            statusBar()->showMessage(error.isEmpty() ? QString("Could not save password securely.") : error, 6000);
        }
    } else {
        CredentialStore::deletePassword(m_pendingLoginAddress, m_pendingLoginPort, m_pendingLoginAccount);
        settings.setValue("Account/RememberPassword", false);
        settings.setValue("Account/AutoLogin", false);
    }

    clearPendingLogin();
}


void MainWindow::showLoginDialog()
{
    if (tryAutoLogin()) {
        return;
    }

    auto *dialog = new LoginDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    QSettings settings;
    const QString address = settings.value("Account/MetaServer", "meta-server.palestar.com").toString();
    const int port = settings.value("Account/MetaPort", 9000).toInt();
    dialog->setConnectionDefaults(address, port);
    const bool remember = settings.value("Account/RememberName", false).toBool();
    dialog->setRememberAccountName(remember);
    dialog->setRememberPassword(settings.value("Account/RememberPassword", false).toBool());
    dialog->setAutoLogin(settings.value("Account/AutoLogin", false).toBool());
    if (remember) {
        const QString account = settings.value("Account/Name").toString();
        dialog->setAccountName(account);
        if (dialog->rememberPassword()) {
            QString password;
            if (CredentialStore::readPassword(address, port, account, &password)) {
                dialog->setPassword(password);
            }
        }
    }

    connect(dialog, &LoginDialog::loginRequested, this, [this, dialog](const QString &address, int port, const QString &account, const QString &password) {
        const bool rememberPassword = dialog->rememberPassword();
        const bool autoLogin = dialog->autoLogin() && rememberPassword;
        // Auto-login and saved passwords both require a saved account name.
        const bool rememberName = dialog->rememberAccountName() || rememberPassword || autoLogin;

        m_pendingLoginAddress = address;
        m_pendingLoginPort = port;
        m_pendingLoginAccount = account;
        m_pendingLoginPassword = password;
        m_pendingRememberName = rememberName;
        m_pendingRememberPassword = rememberPassword;
        m_pendingAutoLogin = autoLogin;
        clearProfile();
        m_bridge->connectAndLogin(address, port, account, password);
    });
    connect(m_bridge, &MetaClientBridge::connectionStateChanged, dialog, &LoginDialog::setStatus);
    connect(m_bridge, &MetaClientBridge::loginFailed, dialog, [dialog](int, const QString &message) {
        dialog->setBusy(false, message, true);
    });
    connect(m_bridge, &MetaClientBridge::loginSucceeded, dialog, [dialog](const QString &, quint32, quint32) {
        dialog->accept();
    });

    dialog->open();
}

void MainWindow::showOptionsDialog()
{
    auto *dialog = new SettingsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &SettingsDialog::themeApplied, this, [this]() {
        applyThemeAppearance();
        statusBar()->showMessage("Theme applied.", 2000);
    });
    connect(dialog, &QDialog::accepted, this, [this]() {
        statusBar()->showMessage("Options saved.", 3000);
    });
    dialog->open();
}

void MainWindow::showAboutDialog()
{
    showGameCQAboutDialog(this);
}

void MainWindow::showChatCommandsDialog()
{
    showChatCommandsHelpDialog(this, m_sessionId, m_profileName, m_profileFlags, isModeratorProfile(), isStaffProfile());
}

void MainWindow::showChangeNameDialog()
{
    bool accepted = false;
    QString suggestedName = suggestedAccountName(m_profileName);
    if (suggestedName.isEmpty()) {
        suggestedName = QSettings().value("Account/Name").toString().trimmed();
    }

    const QString name = QInputDialog::getText(
        this,
        "Change Name",
        "New account name (no clan tags or status text)",
        QLineEdit::Normal,
        suggestedName,
        &accepted);

    if (!accepted) {
        return;
    }

    const QString trimmed = name.trimmed();
    const QString validationError = accountNameValidationError(trimmed);
    if (!validationError.isEmpty()) {
        QMessageBox::warning(this, "Change Name", validationError);
        statusBar()->showMessage(validationError, 5000);
        return;
    }

    statusBar()->showMessage(QString("Changing name to %1...").arg(trimmed), 3000);
    m_bridge->changeName(trimmed);
}

