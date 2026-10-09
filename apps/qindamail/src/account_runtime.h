// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One configured account at run time: the protocol objects built from its
// MailAccount (ImapMailProvider per mailbox, or Pop3MailProvider; the
// SmtpMailProvider; one calendar provider per enabled calendar), its OAuth
// session, its mailbox list and its status. MailService owns one per
// account; the built-in local account has none.
//
// AGENT-CONTRACT (docs/mail.md#qindamail-app, D-042):
// - run(service, provider, start, done) is the ONLY way the service starts
//   a remote operation. It applies the credential first — the keyring
//   secret through MailSecrets (C++ read → provider setSecret with the
//   account's mechanism) or the OAuth access token (ensureToken →
//   applyAccessToken) — then starts the request and, when the result is an
//   authentication rejection, refreshes the credential (a forced token
//   refresh, or a fresh keyring read) and retries ONCE. A missing or dead
//   sign-in sets status "needsSignIn" and answers `rejected` without
//   contacting the server.
// - Services: imap, pop3, smtp, caldav, ics, ews, graph, carddav (M76).
//   Secret keys are `<accountId>/<imap|pop3|smtp|caldav|ews|ics|carddav>`
//   (MailSecrets::services());
//   SMTP with sameCredentialsAsIncoming reads the incoming key.
// AGENT-GUARD: a secret or token is never stored here, never logged and
// never put in a result: it passes from the keyring/OAuth session straight
// into the provider's write-only setSecret().
//
// Threading: GUI thread only.

#include "mail_account.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace QindaTK {
class LocalAttachmentStore;
class LocalProductivityStore;
class ImapMailProvider;
class Pop3MailProvider;
class SmtpMailProvider;
}

namespace QindaOffice::Mail {
class CardDavProvider;
class ContactStore;
class MailSecrets;
class OAuth2Session;
}

namespace QindaOffice::Apps::Mail {

class ImapSourceFetcher;
class ProviderCalls;

struct RuntimeContext {
    QindaTK::LocalProductivityStore *store = nullptr;
    QindaTK::LocalAttachmentStore *attachments = nullptr;
    QindaOffice::Mail::MailSecrets *secrets = nullptr;
    ProviderCalls *calls = nullptr;
    QindaOffice::Mail::ContactStore *contacts = nullptr;   // M76: CardDAV books sync into it
};

struct CalendarBinding {
    QString id;          // calendarId on the records: "<accountId>:<entry id>"
    QString entryId;     // the account's calendar entry id
    QString name;
    QString color;
    QString service;     // caldav | ics | ews | graph
    bool readOnly = false;
    QPointer<QObject> provider;
};

class AccountRuntime : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(const QVariantMap &result)>;
    using Starter = std::function<bool(const QString &requestId)>;

    AccountRuntime(const QindaOffice::Mail::MailAccount &account, const RuntimeContext &context,
                   QObject *parent = nullptr);
    ~AccountRuntime() override;

    [[nodiscard]] const QindaOffice::Mail::MailAccount &account() const { return m_account; }
    [[nodiscard]] QString id() const { return m_account.id; }
    [[nodiscard]] bool isImap() const;
    [[nodiscard]] bool isPop3() const;
    [[nodiscard]] bool canSend() const { return m_smtp != nullptr; }

    // ---- protocol objects (owned) ----
    // The IMAP provider for one mailbox, created on first use. IMAP sync and
    // actions are per mailbox (the provider's `mailbox` decides which records
    // a result touches), so each mailbox gets its own object.
    [[nodiscard]] QindaTK::ImapMailProvider *imap(const QString &mailbox);
    // Whole-message downloads for this IMAP account (M64, imap_source_fetcher.h),
    // created on first use; null for a non-IMAP account.
    [[nodiscard]] ImapSourceFetcher *sourceFetcher();
    [[nodiscard]] QindaTK::Pop3MailProvider *pop3() const { return m_pop3; }
    [[nodiscard]] QindaTK::SmtpMailProvider *smtp() const { return m_smtp; }
    [[nodiscard]] const QList<CalendarBinding> &calendars() const { return m_calendars; }
    // M76: the account's CardDAV address books (service "carddav"), or null.
    [[nodiscard]] QindaOffice::Mail::CardDavProvider *carddav() const { return m_carddav; }
    [[nodiscard]] const CalendarBinding *calendar(const QString &calendarId) const;
    [[nodiscard]] QindaOffice::Mail::OAuth2Session *oauth() const { return m_oauth; }
    [[nodiscard]] QString oauthConfigureError() const { return m_oauthError; }

    // ---- credential routing ----
    [[nodiscard]] QString secretServiceFor(const QString &service) const;
    // The provider mechanism: imap auto|plain|login|xoauth2, pop3
    // auto|user|plain|xoauth2, smtp auto|plain|login|xoauth2, caldav/ics/ews
    // basic|bearer, graph bearer.
    [[nodiscard]] QString mechanismFor(const QString &service) const;
    [[nodiscard]] bool usesOAuth(const QString &service) const;
    // mail (imap/pop3/smtp), calendar (caldav/graph), ews.
    [[nodiscard]] static QString oauthResourceFor(const QString &service);
    // testAccount(): the dialog's password instead of the keyring.
    void setPasswordOverride(const QString &password) { m_passwordOverride = password; }

    void run(const QString &service, QObject *provider, Starter start, Done done);

    // ---- status ----
    // idle | syncing | offline | error | needsSignIn
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QDateTime lastSync() const { return m_lastSync; }
    [[nodiscard]] int pending() const { return m_pending; }
    void setStatus(const QString &status, const QString &error = {});
    void markSynced();
    // Folds one finished request into the status (auth → needsSignIn,
    // offline → offline, accepted → idle).
    void noteResult(const QVariantMap &result);

    // ---- mailboxes (IMAP: the last LIST; others: fixed) ----
    [[nodiscard]] QVariantList mailboxes() const { return m_mailboxes; }
    void setMailboxes(const QVariantList &mailboxes);
    // The mailbox holding `role` (sent, drafts, trash, archive, junk): the
    // server's SPECIAL-USE answer first, then the account's specialFolders.
    [[nodiscard]] QString specialMailbox(const QString &role) const;
    // Whether the server files sent mail itself (Gmail, Microsoft 365).
    [[nodiscard]] bool serverFilesSentMail() const;

    // True for results the server refused because of the credentials.
    [[nodiscard]] static bool isAuthRejection(const QVariantMap &result);

signals:
    void statusChanged();
    void pendingChanged();

private:
    void build();
    void buildCalendars();
    // Applies the credential for `service` to `provider`; `ready` gets {} on
    // success or a failure result. `forceRefresh` after an auth rejection.
    void applyCredentials(const QString &service, QObject *provider, bool forceRefresh,
                          std::function<void(const QVariantMap &failure)> ready);
    // The keyring secret `<account>/<secretService>` into the provider's setSecret.
    void readSecretInto(const QString &secretService, QPointer<QObject> target, const QString &mechanism,
                        std::function<void(const QVariantMap &failure)> ready, bool optional = false);
    void startAttempt(const QString &service, QObject *provider, const Starter &start, const Done &done,
                      int attempt);
    void adjustPending(int delta);

    QindaOffice::Mail::MailAccount m_account;
    RuntimeContext m_context;
    QHash<QString, QindaTK::ImapMailProvider *> m_imap;
    ImapSourceFetcher *m_sourceFetcher = nullptr;
    QindaTK::Pop3MailProvider *m_pop3 = nullptr;
    QindaTK::SmtpMailProvider *m_smtp = nullptr;
    QList<CalendarBinding> m_calendars;
    QindaOffice::Mail::CardDavProvider *m_carddav = nullptr;
    QindaOffice::Mail::OAuth2Session *m_oauth = nullptr;
    QString m_oauthError;
    QString m_passwordOverride;
    QVariantList m_mailboxes;
    QString m_status = QStringLiteral("idle");
    QString m_lastError;
    QDateTime m_lastSync;
    int m_pending = 0;
};

} // namespace QindaOffice::Apps::Mail
