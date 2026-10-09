// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// An Exchange or Microsoft 365 mailbox's settings and item tags (M81,
// D-090; docs/mail.md#exchange-and-microsoft-365-mailboxes): automatic
// replies, the master category list, the server's inbox rules (shown
// read-only) and the categories and follow-up flag of a message (found by
// its Internet message id) or of a synced event (by its remote id) —
// over Microsoft Graph (`service: "graph"`) or Exchange Web Services
// (`service: "ews"`). A host adapter with the toolkit providers' surface,
// so AccountRuntime::run applies its credential like any other's.
//
// AGENT-CONTRACT (MailOrganizer, AccountRuntime, tests): one request in,
// ONE `…Finished(requestId, result)` out on a later event-loop turn;
// result = {requestId, ok, state accepted | offline | rejected, error,
// retryable, transcript, [authRejected], [retryAfterSeconds]} plus
//   autoRepliesFinished  settings {enabled, scheduled, start, end (ISO UTC),
//                        internalMessage, externalMessage (plain text),
//                        externalAudience none | contacts | all}
//   categoriesFinished   categories [{name, preset 0..24 | -1, id}]
//                        (addCategory: the list after the add)
//   serverRulesFinished  rules [{name, enabled, order, summary}]
//   itemStateFinished    setItemState: found, remoteId;
//                        pullItemStates: items [{internetMessageId,
//                        categories, flag {status, start, due, completed}}]
// setItemState input: {kind "message" | "event", internetMessageId (bare,
// without <>), remoteId (event), folder (the IMAP mailbox name, EWS only:
// where to look), categories [names] (absent = unchanged), flag {status
// "flagged" | "complete" | "" , start, due "yyyy-MM-dd", completed ISO}
// (absent = unchanged), zone (IANA, for the flag's dates)}.
// EWS keeps the master list read-only (Outlook's CategoryList user
// configuration); addCategory over EWS is refused with a reason.
// AGENT-GUARD: the secret lives only in memory; results, transcripts and
// errors are redacted (HttpTransport), credentials go only to https, a
// loopback host, or with allowInsecureAuth.

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Mail {

class ExchangeMailbox : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString service MEMBER m_service NOTIFY configChanged)
    Q_PROPERTY(QString baseUrl MEMBER m_baseUrl NOTIFY configChanged)
    Q_PROPERTY(QString url MEMBER m_url NOTIFY configChanged)
    Q_PROPERTY(QString userName MEMBER m_userName NOTIFY configChanged)
    Q_PROPERTY(QString mailbox MEMBER m_mailbox NOTIFY configChanged)
    Q_PROPERTY(QString serverVersion MEMBER m_serverVersion NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs MEMBER m_timeoutMs NOTIFY configChanged)
    Q_PROPERTY(bool allowInsecureAuth MEMBER m_allowInsecureAuth NOTIFY configChanged)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY secretChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
public:
    explicit ExchangeMailbox(QObject *parent = nullptr);
    ~ExchangeMailbox() override;

    [[nodiscard]] bool hasSecret() const { return !m_secret.isEmpty(); }
    [[nodiscard]] bool busy() const { return m_pending > 0; }

    // Graph: "bearer"; EWS: "basic" (password, with userName) or "bearer".
    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("bearer"));
    Q_INVOKABLE void clearSecret();

    Q_INVOKABLE bool readAutoReplies(const QString &requestId);
    Q_INVOKABLE bool writeAutoReplies(const QString &requestId, const QVariantMap &settings);
    Q_INVOKABLE bool listCategories(const QString &requestId);
    Q_INVOKABLE bool addCategory(const QString &requestId, const QString &name, int preset);
    Q_INVOKABLE bool listServerRules(const QString &requestId);
    Q_INVOKABLE bool setItemState(const QString &requestId, const QVariantMap &item);
    // Messages received in the last `days` that carry categories or a flag.
    Q_INVOKABLE bool pullItemStates(const QString &requestId, int days);

signals:
    void configChanged();
    void secretChanged();
    void stateChanged();
    void autoRepliesFinished(const QString &requestId, const QVariantMap &result);
    void categoriesFinished(const QString &requestId, const QVariantMap &result);
    void serverRulesFinished(const QString &requestId, const QVariantMap &result);
    void itemStateFinished(const QString &requestId, const QVariantMap &result);

private:
    enum class Reply { AutoReplies, Categories, Rules, ItemState };
    bool start(int op, Reply reply, const QString &requestId, const QVariantMap &input);
    void deliver(Reply reply, const QString &requestId, const QVariantMap &result);

    QString m_service = QStringLiteral("graph");
    QString m_baseUrl = QStringLiteral("https://graph.microsoft.com/v1.0");
    QString m_url, m_userName, m_mailbox;
    QString m_serverVersion = QStringLiteral("Exchange2013_SP1");
    int m_timeoutMs = 15000;
    bool m_allowInsecureAuth = false;
    QString m_secret, m_mechanism = QStringLiteral("bearer");
    int m_pending = 0;
};

} // namespace QindaOffice::Mail
