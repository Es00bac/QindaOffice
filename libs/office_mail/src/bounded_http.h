// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One HTTP exchange with the limits every office_mail request needs: https
// only (http only to loopback, for tests and local bridges), redirects
// checked against the same rule, a deadline, and a response-size cap. The
// body is read incrementally, so an endless or huge response costs at most
// `maxBytes` of memory.

#include <QByteArray>
#include <QNetworkRequest>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QObject;

namespace QindaOffice::Mail {

struct HttpOutcome {
    enum class Kind {
        Ok,         // 2xx
        HttpError,  // a response with a non-2xx status (body kept)
        Network,    // no connection, DNS, TLS, reset …
        Timeout,    // the deadline passed
        TooLarge,   // the body exceeded maxBytes
        Refused,    // the URL (or a redirect) broke the https/loopback rule
        Aborted,    // cancelled by the caller
    };
    Kind kind = Kind::Network;
    int status = 0;
    QByteArray body;
    QString error;      // for logs and results; never includes the request body
    QUrl url;           // the final URL (after redirects)

    // The server could not be reached — worth retrying later.
    [[nodiscard]] bool offline() const { return kind == Kind::Network || kind == Kind::Timeout; }
};

struct HttpLimits {
    int timeoutMs = 15000;
    qint64 maxBytes = 256 * 1024;
    bool followRedirects = true;    // GET only; POSTs never follow
    int maxRedirects = 5;
};

class BoundedHttp {
public:
    using Done = std::function<void(const HttpOutcome &)>;

    // Sends `verb` (GET or POST) and calls `done` exactly once, on
    // `context`'s thread, unless `context` is destroyed first. Returns the
    // reply (for cancel()), or null when the URL was refused — `done` then
    // runs on the next event-loop turn.
    static QPointer<QNetworkReply> send(QNetworkAccessManager *manager, QNetworkRequest request,
                                        const QByteArray &verb, const QByteArray &body, const HttpLimits &limits,
                                        QObject *context, Done done);

    // Aborts a running exchange; its `done` reports Kind::Aborted.
    static void cancel(const QPointer<QNetworkReply> &reply);
};

} // namespace QindaOffice::Mail
