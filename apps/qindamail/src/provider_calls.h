// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One correlation point for every protocol object QindaMail drives: the
// toolkit providers (IMAP, POP3, SMTP, CalDAV, ICS), the Exchange/Graph
// adapters, MailSecrets, OAuth2Session and AutoConfig all answer a request
// with ONE `…Finished(requestId, result)` (or `finished`) signal. watch()
// connects each of them once; expect() registers a callback for one
// request id; the first result with that id runs it and forgets it.
//
// AGENT-CONTRACT (QindaTK docs/productivity-providers.md "Common result
// contract"): results carry requestId, ok, state (accepted | offline |
// rejected | conflict), error, retryable. Request ids come from
// newRequestId() and are unique per process, so one dispatcher can serve
// every provider. A result nobody expects (a QML caller's own request) is
// ignored.
//
// Threading: GUI thread only; providers deliver on the GUI thread (D-186).

#include <QHash>
#include <QObject>
#include <QVariantMap>

#include <functional>

namespace QindaOffice::Apps::Mail {

class ProviderCalls : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(const QVariantMap &result)>;

    explicit ProviderCalls(QObject *parent = nullptr);

    // Connects every signal of `provider` named `finished` or ending in
    // `Finished` with the (QString, QVariantMap) signature. Idempotent.
    void watch(QObject *provider);
    void expect(const QString &requestId, Callback callback);
    [[nodiscard]] bool isPending(const QString &requestId) const { return m_callbacks.contains(requestId); }
    [[nodiscard]] int pendingCount() const { return int(m_callbacks.size()); }

    // "<prefix>-<n>-<uuid fragment>": unique per process and readable in logs.
    [[nodiscard]] static QString newRequestId(const QString &prefix);

    // A result map in the contract's shape, for failures decided locally.
    [[nodiscard]] static QVariantMap result(const QString &requestId, const QString &state,
                                            const QString &error = {}, bool retryable = false);

public slots:
    void deliver(const QString &requestId, const QVariantMap &result);

private:
    QHash<QString, Callback> m_callbacks;
};

} // namespace QindaOffice::Apps::Mail
