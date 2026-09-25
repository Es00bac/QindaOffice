// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The list of configured mail/calendar accounts, persisted atomically as
// JSON (schema `qindaoffice.mail.accounts/1`) at
// $XDG_CONFIG_HOME/qindaoffice/mail/accounts.json. Configuration only: the
// file never holds a secret, and validation refuses secret-looking keys.
//
// Every mutation validates, writes the file, and only then changes the
// in-memory list, so what QML shows is what is on disk. A file written by a
// newer schema is loaded read-only (never downgraded); an unparseable file
// is moved aside to accounts.json.corrupt, like Core::JsonSettings.
//
// Threading: GUI thread only. Ownership: `secrets` is not owned; without
// one, remove() uses a MailSecrets child over defaultSecretBackend().

#include "mail_account.h"
#include "mail_secrets.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace QindaOffice::Mail {

class AccountStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // Empty means the default path; resolved (and the directory created) on
    // first use. Setting it reloads.
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(int count READ count NOTIFY accountsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(QindaOffice::Mail::MailSecrets *secrets READ secrets WRITE setSecrets NOTIFY secretsChanged)
    Q_PROPERTY(QStringList kinds READ kinds CONSTANT)
    // Every built-in provider preset as a map (provider_presets.h).
    Q_PROPERTY(QVariantList presets READ presets CONSTANT)

public:
    static constexpr const char *kSchema = "qindaoffice.mail.accounts/1";

    explicit AccountStore(QObject *parent = nullptr);
    AccountStore(const QString &path, QObject *parent = nullptr);

    [[nodiscard]] static QString defaultPath();

    [[nodiscard]] QString path() const;
    void setPath(const QString &path);
    [[nodiscard]] QVariantList accounts() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] bool readOnly() const { return m_readOnly; }
    [[nodiscard]] MailSecrets *secrets() const { return m_secrets; }
    void setSecrets(MailSecrets *secrets);
    [[nodiscard]] QStringList kinds() const { return accountKinds(); }
    [[nodiscard]] QVariantList presets() const;

    // C++ view of the same list.
    [[nodiscard]] QList<MailAccount> list() const;
    [[nodiscard]] std::optional<MailAccount> find(const QString &id) const;

    // Re-reads the file. False (with lastError) when it could not be read;
    // the list is then whatever could be recovered (empty after quarantine).
    Q_INVOKABLE bool reload();

    // add/update return {ok, id, error, errors (QStringList, one per field)}.
    // add() generates the id when the map has none; update() needs a known id.
    // Both fill defaults first (normalized()): display name and user names
    // from the address, the port from protocol + security.
    Q_INVOKABLE QVariantMap add(const QVariantMap &account);
    Q_INVOKABLE QVariantMap update(const QVariantMap &account);
    // Returns {ok, id, error, secretsRequestId}. After the file is written
    // the account's keyring secrets are removed (MailSecrets::removeAccount);
    // secretsRemoved(id, result) reports that second step.
    Q_INVOKABLE QVariantMap remove(const QString &id);
    // {} when unknown.
    Q_INVOKABLE QVariantMap accountById(const QString &id) const;
    // Validation without saving (for the dialog): one line per problem.
    Q_INVOKABLE QStringList validate(const QVariantMap &account) const;
    // A candidate account for `kind` and `emailAddress`: from the matching
    // preset when there is one (the preset's kind wins), else defaults.
    Q_INVOKABLE QVariantMap draftFor(const QString &kind, const QString &emailAddress,
                                     const QString &fullName = {}) const;

    // Defaults the dialog does not need to ask for (see add()).
    [[nodiscard]] static MailAccount normalized(MailAccount account);

signals:
    void pathChanged();
    void accountsChanged();
    void lastErrorChanged();
    void readOnlyChanged();
    void secretsChanged();
    void accountAdded(const QString &id);
    void accountUpdated(const QString &id);
    void accountRemoved(const QString &id);
    void secretsRemoved(const QString &id, const QVariantMap &result);

private:
    void ensureLoaded() const;
    bool loadFile();
    [[nodiscard]] bool save(const QList<MailAccount> &accounts, QString *error);
    void setError(const QString &error);
    void setReadOnly(bool readOnly);
    QVariantMap failure(const QString &id, const QString &error, const QStringList &errors = {});
    MailSecrets *secretsForRemoval();

    QString m_path;
    mutable bool m_loaded = false;
    QList<MailAccount> m_accounts;
    QJsonObject m_rootExtra;   // unknown top-level keys, preserved on save
    QString m_lastError;
    bool m_readOnly = false;
    QPointer<MailSecrets> m_secrets;
    MailSecrets *m_ownSecrets = nullptr;
    int m_removeSerial = 0;
};

} // namespace QindaOffice::Mail
