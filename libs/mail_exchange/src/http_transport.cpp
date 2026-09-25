// SPDX-License-Identifier: GPL-3.0-or-later
#include "http_transport.h"

#include "exchange_common.h"

#include <QDateTime>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

// Off by default; QT_LOGGING_RULES="qindaoffice.mail.exchange.debug=true"
// mirrors the (already redacted) transcript for field diagnosis.
Q_LOGGING_CATEGORY(lcExchange, "qindaoffice.mail.exchange", QtWarningMsg)

namespace QindaOffice::Mail {
namespace {
constexpr qint64 kMaxBody = 16 * 1024 * 1024;

QString target(const QUrl &url)
{
    return url.toString(QUrl::RemoveUserInfo | QUrl::FullyEncoded);
}
} // namespace

HttpTransport::HttpTransport(int timeoutMs, bool allowInsecureAuth)
    : m_manager(std::make_unique<QNetworkAccessManager>()), m_timeoutMs(timeoutMs > 0 ? timeoutMs : 15000),
      m_allowInsecureAuth(allowInsecureAuth)
{
    m_manager->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
}

HttpTransport::~HttpTransport() = default;

void HttpTransport::setAuthorization(const QByteArray &authorization, const QStringList &secrets)
{
    m_authorization = authorization;
    m_secrets = secrets;
}

QString HttpTransport::redact(const QString &text) const
{
    return QindaTK::Protocol::redact(text, m_secrets);
}

void HttpTransport::fail(Failure failure, const QString &error, int retryAfterSeconds)
{
    if (m_failure != Failure::None) return;     // the first failure explains the result
    m_failure = failure;
    if (retryAfterSeconds >= 0) m_retryAfter = retryAfterSeconds;
    m_error = redact(error);
    note(QStringLiteral("! ") + m_error);
}

void HttpTransport::note(const QString &line)
{
    // AGENT-GUARD: redact before the line leaves this object, log included.
    m_transcript.append(redact(line));
    qCDebug(lcExchange).noquote() << m_transcript.last();
}

bool HttpTransport::send(const QByteArray &method, const QUrl &url, const Headers &headers, const QByteArray &body,
                         const QString &label, Response *response)
{
    *response = {};
    if (m_failure != Failure::None) return false;
    const bool https = url.scheme() == QLatin1String("https");
    if (!https && url.scheme() != QLatin1String("http")) {
        fail(Failure::Rejected, QStringLiteral("Unsupported URL scheme in %1").arg(target(url)));
        return false;
    }
    if (!m_authorization.isEmpty() && !QindaTK::Protocol::plaintextAuthAllowed(url.host(), https, m_allowInsecureAuth)) {
        fail(Failure::Rejected, QStringLiteral("Refusing to send credentials over plain HTTP to %1").arg(url.host()));
        return false;
    }
    QNetworkRequest request(url);
    request.setTransferTimeout(m_timeoutMs);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("QindaOffice-Mail/0.1"));
    for (const auto &[name, value] : headers) request.setRawHeader(name, value);
    note(QStringLiteral("> %1 %2%3 (%4 bytes)").arg(QString::fromLatin1(method), target(url),
         label.isEmpty() ? QString() : QStringLiteral(" [") + label + QLatin1Char(']')).arg(body.size()));
    if (!m_authorization.isEmpty()) {
        request.setRawHeader("Authorization", m_authorization);
        note(QStringLiteral("> Authorization: [credentials redacted]"));
    }
    QNetworkReply *reply = m_manager->sendCustomRequest(request, method, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    if (!reply->isFinished()) loop.exec();
    std::unique_ptr<QNetworkReply, void (*)(QNetworkReply *)> owned(reply, [](QNetworkReply *r) { r->deleteLater(); });

    response->status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    for (const auto &[name, value] : reply->rawHeaderPairs()) response->headers.insert(name.toLower(), value);
    const QNetworkReply::NetworkError error = reply->error();
    if (response->status == 0) {
        const bool tls = error == QNetworkReply::SslHandshakeFailedError;
        const QString reason = error == QNetworkReply::OperationCanceledError
            ? QStringLiteral("no answer within %1 ms").arg(m_timeoutMs) : reply->errorString();
        fail(tls ? Failure::Rejected : Failure::Offline,
             tls ? QStringLiteral("TLS verification failed for %1: %2").arg(url.host(), reason)
                 : QStringLiteral("Could not reach %1: %2").arg(url.host(), reason));
        return false;
    }
    response->body = reply->read(kMaxBody + 1);
    note(QStringLiteral("< HTTP %1 (%2 bytes)").arg(response->status).arg(response->body.size()));
    if (response->body.size() > kMaxBody) {
        fail(Failure::Rejected, QStringLiteral("The server response exceeds 16 MiB"));
        return false;
    }
    if (response->status == 401 || response->status == 403) {
        m_authRejected = true;
        fail(Failure::Rejected, QStringLiteral("The server refused the credentials (HTTP %1); sign in again or check "
                                               "the password or token").arg(response->status));
        return false;
    }
    if (response->status == 429 || response->status == 503) {
        const int after = parseRetryAfter(response->headers.value("retry-after"), QDateTime::currentDateTimeUtc());
        fail(Failure::Offline, after >= 0
            ? QStringLiteral("The server is throttling requests (HTTP %1); retry in %2 s").arg(response->status).arg(after)
            : QStringLiteral("The server is busy (HTTP %1); retry later").arg(response->status), after);
        return false;
    }
    return true;
}

} // namespace QindaOffice::Mail
