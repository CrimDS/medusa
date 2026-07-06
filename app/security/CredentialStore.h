#pragma once

#include <QString>

namespace CredentialStore {

QString targetName(const QString &address, int port, const QString &account);
bool writePassword(const QString &address, int port, const QString &account, const QString &password, QString *error = nullptr);
bool readPassword(const QString &address, int port, const QString &account, QString *password, QString *error = nullptr);
bool deletePassword(const QString &address, int port, const QString &account, QString *error = nullptr);
bool hasPassword(const QString &address, int port, const QString &account);

} // namespace CredentialStore
