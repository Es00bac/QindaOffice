// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One JSON-over-HTTPS exchange with a bank or payment API (M48, D-048):
// the transport rules every books_connect request follows.
//
//  * https:// only; http:// only to a loopback host (the tests' fake
//    servers). Names are not resolved: a DNS name that happens to point at
//    127.0.0.1 is NOT loopback. A refused URL never opens a socket.
//  * Redirects are never followed — the APIs do not redirect, and
//    following one would carry the Authorization header somewhere else.
//  * A deadline, a response-size cap, no cookies, no cache.
//  * Error text is redacted: the credential and anything shaped like a
//    Mercury/Stripe token never appear in `error` (the servers' own error
//    messages can echo part of a key).
//
// AGENT-GUARD: never log a request's headers or a response body here.
// Callers put `error` into results, the company file's last_error and the
// UI; redact() is what keeps tokens out of all three (tests assert it).

#include <QByteArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QObject;

namespace QindaOffice::Books::Connect {

struct ApiResponse {
    enum class Kind {
        Ok,           // 2xx with a JSON body
        Unauthorized, // 401 / 403: the token or key was refused
        HttpError,    // any other non-2xx status (json kept when it parsed)
        BadJson,      // 2xx but the body is not JSON
        Network,      // no connection, DNS, TLS, reset …
        Timeout,
        TooLarge,
        Refused,      // the URL broke the https/loopback rule
    };
    Kind kind = Kind::Network;
    int status = 0;
    QJsonDocument json;
    QString error; // for people; redacted

    [[nodiscard]] bool ok() const { return kind == Kind::Ok; }
    // Worth trying again later without changing anything.
    [[nodiscard]] bool retryable() const { return kind == Kind::Network || kind == Kind::Timeout || status >= 500; }
};

struct HttpLimits {
    int timeoutMs = 30000;
    qint64 maxBytes = 16 * 1024 * 1024;
};

class ConnectHttp {
public:
    using Done = std::function<void(const ApiResponse &)>;

    // https anywhere, http only to loopback. `why` gets a sentence.
    [[nodiscard]] static bool urlAllowed(const QUrl &url, QString *why = nullptr);
    [[nodiscard]] static bool isLoopbackHost(const QString &host);

    // Sends `verb` with `body` and calls `done` exactly once on `context`'s
    // thread, unless `context` is destroyed first (then never). `secret`
    // is only used to redact error text. A refused URL completes on the
    // next event-loop turn with Kind::Refused.
    static void send(QNetworkAccessManager *manager, QNetworkRequest request, const QByteArray &verb,
                     const QByteArray &body, const QString &secret, QObject *context, Done done,
                     const HttpLimits &limits = {});

    // Replaces `secret` and every token-shaped word with "[redacted]".
    [[nodiscard]] static QString redact(QString text, const QString &secret);
};

} // namespace QindaOffice::Books::Connect
