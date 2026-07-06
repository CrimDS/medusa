#pragma once

#include <QDialog>

class QLabel;
class QCheckBox;
class QLineEdit;
class QPushButton;
class QSpinBox;

class LoginDialog final : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);

    QString accountName() const;
    bool rememberAccountName() const;
    bool rememberPassword() const;
    bool autoLogin() const;
    void setAccountName(const QString &account);
    void setPassword(const QString &password);
    void setRememberAccountName(bool remember);
    void setRememberPassword(bool remember);
    void setAutoLogin(bool enabled);
    void setConnectionDefaults(const QString &address, int port);
    void setBusy(bool busy, const QString &message = QString(), bool error = false);
    void setStatus(const QString &message);

signals:
    void loginRequested(const QString &address, int port, const QString &account, const QString &password);

private slots:
    void submit();

private:
    QLineEdit *m_address = nullptr;
    QSpinBox *m_port = nullptr;
    QLineEdit *m_account = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_remember = nullptr;
    QCheckBox *m_rememberPassword = nullptr;
    QCheckBox *m_autoLogin = nullptr;
    QLabel *m_message = nullptr;
    QPushButton *m_loginButton = nullptr;
};
