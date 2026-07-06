#include "security/CredentialStore.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

#include <string>

namespace {

QString normalizedAccount(const QString &account)
{
    return account.trimmed().toLower();
}

QString normalizedAddress(const QString &address)
{
    return address.trimmed().toLower();
}

#ifdef Q_OS_WIN
QString windowsErrorMessage(DWORD code)
{
    if (code == ERROR_NOT_FOUND) {
        return {};
    }
    return QString("Windows Credential Manager error %1.").arg(code);
}
#endif

} // namespace

namespace CredentialStore {

QString targetName(const QString &address, int port, const QString &account)
{
    return QString("GameCQ2/%1:%2/%3").arg(normalizedAddress(address)).arg(port).arg(normalizedAccount(account));
}

bool writePassword(const QString &address, int port, const QString &account, const QString &password, QString *error)
{
    if (error) {
        error->clear();
    }

    if (address.trimmed().isEmpty() || account.trimmed().isEmpty() || password.isEmpty()) {
        if (error) {
            *error = "Missing account, server, or password.";
        }
        return false;
    }

#ifdef Q_OS_WIN
    const std::wstring target = targetName(address, port, account).toStdWString();
    const std::wstring user = account.trimmed().toStdWString();
    const std::wstring secret = password.toStdWString();

    CREDENTIALW credential = {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target.c_str());
    credential.UserName = const_cast<LPWSTR>(user.c_str());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t *>(secret.data()));
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size() * sizeof(wchar_t));
    // Durable Windows generic credentials are stored in the current user's vault
    // with this persistence level; the API has no CRED_PERSIST_LOCAL_USER value.
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;

    if (CredWriteW(&credential, 0)) {
        return true;
    }

    if (error) {
        *error = windowsErrorMessage(GetLastError());
    }
    return false;
#else
    if (error) {
        *error = "Secure password storage is only implemented on Windows.";
    }
    return false;
#endif
}

bool readPassword(const QString &address, int port, const QString &account, QString *password, QString *error)
{
    if (password) {
        password->clear();
    }
    if (error) {
        error->clear();
    }

    if (address.trimmed().isEmpty() || account.trimmed().isEmpty()) {
        return false;
    }

#ifdef Q_OS_WIN
    const std::wstring target = targetName(address, port, account).toStdWString();
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        if (error) {
            *error = windowsErrorMessage(GetLastError());
        }
        return false;
    }

    if (password && credential->CredentialBlob && credential->CredentialBlobSize > 0) {
        const auto length = static_cast<int>(credential->CredentialBlobSize / sizeof(wchar_t));
        *password = QString::fromWCharArray(reinterpret_cast<const wchar_t *>(credential->CredentialBlob), length);
    }
    CredFree(credential);
    return password && !password->isEmpty();
#else
    if (error) {
        *error = "Secure password storage is only implemented on Windows.";
    }
    return false;
#endif
}

bool deletePassword(const QString &address, int port, const QString &account, QString *error)
{
    if (error) {
        error->clear();
    }

    if (address.trimmed().isEmpty() || account.trimmed().isEmpty()) {
        return true;
    }

#ifdef Q_OS_WIN
    const std::wstring target = targetName(address, port, account).toStdWString();
    if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) {
        return true;
    }

    const DWORD code = GetLastError();
    if (code == ERROR_NOT_FOUND) {
        return true;
    }
    if (error) {
        *error = windowsErrorMessage(code);
    }
    return false;
#else
    if (error) {
        *error = "Secure password storage is only implemented on Windows.";
    }
    return false;
#endif
}

bool hasPassword(const QString &address, int port, const QString &account)
{
    QString password;
    return readPassword(address, port, account, &password);
}

} // namespace CredentialStore
