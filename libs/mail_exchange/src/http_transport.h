// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One sequential HTTP dialogue for an EWS or Graph job, run on a worker
// thread: each send() blocks on a job-local QNetworkAccessManager until the
// reply finishes, records a redacted transcript line pair and classifies the
// first failure as QindaTK::Protocol::Failure (offline vs rejected).
//
// AGENT-NOTE: the adapters mirror QindaTK's D-186 shape (one exchange per
// request on a worker, result applied on the GUI thread) so the sync logic
// reads top to bottom like CalDavJobs. QNetworkAccessManager (not the
// toolkit's raw socket channel) is used for HTTP/2, chunked/compressed
// bodies and the system proxy, which corporate Exchange needs. The manager
// is created and destroyed inside the job thread and never shared.
// AGENT-GUARD: credentials go only to https, a loopback host or an
// explicitly allowed insecure endpoint (QindaTK D-187); send() refuses
// otherwise even for URLs a server handed us (Graph @odata.nextLink).
// Redirects are never followed, so an Authorization header cannot be
// forwarded to another origin.

#include <qindatk/protocol_client.h>

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <memory>

class QNetworkAccessManager;

namespace QindaOffice::Mail {

class HttpTransport {
public:
    using Failure = QindaTK::Protocol::Failure;
    using Headers = QList<QPair<QByteArray, QByteArray>>;

    struct Response {
        int status = 0;
        QMap<QByteArray, QByteArray> headers;   // lower-case names
        QByteArray body;
    };

    HttpTransport(int timeoutMs, bool allowInsecureAuth);
    ~HttpTransport();
    HttpTransport(const HttpTransport &) = delete;
    HttpTransport &operator=(const HttpTransport &) = delete;

    // `authorization` is the full header value; `secrets` are redacted
    // (with their base64 forms) from every transcript line and error.
    void setAuthorization(const QByteArray &authorization, const QStringList &secrets);

    // False when the dialogue failed as a whole: no connection, TLS, 401/403
    // (authRejected), 429/503 (retryAfterSeconds), timeout, oversize body,
    // or a refused credential destination. Any other HTTP status returns
    // true and is the caller's to interpret.
    bool send(const QByteArray &method, const QUrl &url, const Headers &headers, const QByteArray &body,
              const QString &label, Response *response);

    // The first failure wins; `retryAfterSeconds` >= 0 marks a throttled server.
    void fail(Failure failure, const QString &error, int retryAfterSeconds = -1);
    void note(const QString &line);
    Failure failure() const { return m_failure; }
    QString error() const { return m_error; }
    QStringList transcript() const { return m_transcript; }
    int retryAfterSeconds() const { return m_retryAfter; }
    bool authRejected() const { return m_authRejected; }
    QString redact(const QString &text) const;

private:
    std::unique_ptr<QNetworkAccessManager> m_manager;
    int m_timeoutMs = 15000;
    bool m_allowInsecureAuth = false;
    QByteArray m_authorization;
    QStringList m_secrets, m_transcript;
    Failure m_failure = Failure::None;
    QString m_error;
    int m_retryAfter = -1;
    bool m_authRejected = false;
};

} // namespace QindaOffice::Mail
