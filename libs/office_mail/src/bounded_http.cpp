// SPDX-License-Identifier: GPL-3.0-or-later
#include "bounded_http.h"

#include "net_policy.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

#include <memory>

namespace QindaOffice::Mail {
namespace {

// Per-exchange bookkeeping, owned by the lambdas connected to the reply.
struct Exchange {
    HttpOutcome::Kind forced = HttpOutcome::Kind::Ok;   // set before abort()
    bool forcedSet = false;
    QString forcedError;
    QByteArray body;
    bool delivered = false;
};

constexpr const char *kAbortProperty = "qqoMailAbort";

} // namespace

QPointer<QNetworkReply> BoundedHttp::send(QNetworkAccessManager *manager, QNetworkRequest request,
                                          const QByteArray &verb, const QByteArray &body, const HttpLimits &limits,
                                          QObject *context, Done done)
{
    QString why;
    if (!isSecureOrLoopbackUrl(request.url(), &why)) {
        HttpOutcome outcome;
        outcome.kind = HttpOutcome::Kind::Refused;
        outcome.error = why;
        outcome.url = request.url();
        QTimer::singleShot(0, context, [done, outcome] { done(outcome); });
        return {};
    }
    const bool follow = limits.followRedirects && verb == "GET";
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         follow ? QNetworkRequest::UserVerifiedRedirectPolicy : QNetworkRequest::ManualRedirectPolicy);
    request.setMaximumRedirectsAllowed(limits.maxRedirects);
    request.setTransferTimeout(limits.timeoutMs);
    // No cookies, no cache: every exchange stands alone.
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);

    QNetworkReply *reply = manager->sendCustomRequest(request, verb, body);
    auto state = std::make_shared<Exchange>();
    const auto force = [state, reply](HttpOutcome::Kind kind, const QString &error) {
        if (state->forcedSet) return;
        state->forcedSet = true;
        state->forced = kind;
        state->forcedError = error;
        reply->abort();
    };

    QObject::connect(reply, &QNetworkReply::redirected, reply, [reply, force](const QUrl &target) {
        QString refused;
        if (isSecureOrLoopbackUrl(target, &refused)) {
            emit reply->redirectAllowed();
        } else {
            force(HttpOutcome::Kind::Refused, QStringLiteral("Redirect refused: ") + refused);
        }
    });
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, force, limits] {
        const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (length > limits.maxBytes) {
            force(HttpOutcome::Kind::TooLarge,
                  QStringLiteral("The response is larger than %1 bytes.").arg(limits.maxBytes));
        }
    });
    QObject::connect(reply, &QIODevice::readyRead, reply, [reply, state, force, limits] {
        state->body += reply->readAll();
        if (state->body.size() > limits.maxBytes) {
            force(HttpOutcome::Kind::TooLarge,
                  QStringLiteral("The response is larger than %1 bytes.").arg(limits.maxBytes));
        }
    });
    // Transfer timeout covers stalls; this covers a server that trickles.
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, reply, [force, limits] {
        force(HttpOutcome::Kind::Timeout, QStringLiteral("No complete answer within %1 s.").arg(limits.timeoutMs / 1000.0));
    });
    deadline->start(limits.timeoutMs);

    QPointer<QObject> guard(context);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, state, guard, done] {
        if (state->delivered) return;
        state->delivered = true;
        reply->deleteLater();
        HttpOutcome outcome;
        outcome.url = reply->url();
        outcome.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!state->forcedSet && reply->property(kAbortProperty).toBool()) {
            state->forcedSet = true;
            state->forced = HttpOutcome::Kind::Aborted;
            state->forcedError = QStringLiteral("Cancelled.");
        }
        if (state->forcedSet) {
            outcome.kind = state->forced;
            outcome.error = state->forcedError;
        } else {
            state->body += reply->readAll();
            outcome.body = state->body;
            if (reply->error() == QNetworkReply::OperationCanceledError && outcome.status == 0) {
                // Qt reports its own transfer timeout as a cancellation.
                outcome.kind = HttpOutcome::Kind::Timeout;
                outcome.error = QStringLiteral("The server did not answer in time.");
            } else if (outcome.status >= 200 && outcome.status < 300) {
                outcome.kind = HttpOutcome::Kind::Ok;
            } else if (outcome.status > 0) {
                outcome.kind = HttpOutcome::Kind::HttpError;
                outcome.error = QStringLiteral("HTTP %1 from %2")
                                    .arg(outcome.status)
                                    .arg(outcome.url.toDisplayString(QUrl::RemoveQuery | QUrl::RemoveUserInfo));
            } else {
                outcome.kind = HttpOutcome::Kind::Network;
                outcome.error = reply->errorString();
            }
        }
        if (guard) done(outcome);
    });
    return reply;
}

void BoundedHttp::cancel(const QPointer<QNetworkReply> &reply)
{
    if (!reply || reply->isFinished()) return;
    reply->setProperty(kAbortProperty, true);
    reply->abort();
}

} // namespace QindaOffice::Mail
