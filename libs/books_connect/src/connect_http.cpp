// SPDX-License-Identifier: GPL-3.0-or-later
#include "connect_http.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>

#include <memory>

namespace QindaOffice::Books::Connect {

namespace {

struct Exchange {
    QByteArray body;
    bool delivered = false;
    bool forced = false;
    ApiResponse::Kind forcedKind = ApiResponse::Kind::Network;
    QString forcedError;
};

} // namespace

bool ConnectHttp::isLoopbackHost(const QString &host)
{
    QString h = host.trimmed().toLower();
    if (h.startsWith(QLatin1Char('[')) && h.endsWith(QLatin1Char(']'))) {
        h = h.mid(1, h.size() - 2);
    }
    if (h == QLatin1String("localhost")) {
        return true;
    }
    const QHostAddress address(h);
    return !address.isNull() && address.isLoopback();
}

bool ConnectHttp::urlAllowed(const QUrl &url, QString *why)
{
    if (!url.isValid() || url.host().isEmpty()) {
        if (why != nullptr) {
            *why = QStringLiteral("\"%1\" is not a valid address.").arg(url.toDisplayString(QUrl::RemoveUserInfo));
        }
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https") || (scheme == QLatin1String("http") && isLoopbackHost(url.host()))) {
        return true;
    }
    if (why != nullptr) {
        *why = QStringLiteral("%1 is refused: bank and payment connections use https:// only.")
                   .arg(url.toDisplayString(QUrl::RemoveUserInfo | QUrl::RemoveQuery));
    }
    return false;
}

QString ConnectHttp::redact(QString text, const QString &secret)
{
    if (secret.size() >= 4) {
        text.replace(secret, QStringLiteral("[redacted]"));
    }
    static const QRegularExpression tokens(
        QStringLiteral("secret-token:\\S*|\\b(?:sk|rk|pk)_(?:live|test)_[A-Za-z0-9*]+"));
    text.replace(tokens, QStringLiteral("[redacted]"));
    return text;
}

void ConnectHttp::send(QNetworkAccessManager *manager, QNetworkRequest request, const QByteArray &verb,
                       const QByteArray &body, const QString &secret, QObject *context, Done done,
                       const HttpLimits &limits)
{
    QString why;
    if (!urlAllowed(request.url(), &why)) {
        ApiResponse refused;
        refused.kind = ApiResponse::Kind::Refused;
        refused.error = why;
        QTimer::singleShot(0, context, [done, refused] { done(refused); });
        return;
    }
    // AGENT-GUARD: no redirects (the Authorization header must never
    // follow one), no cookies, no cache.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(limits.timeoutMs);

    QNetworkReply *reply = manager->sendCustomRequest(request, verb, body);
    auto state = std::make_shared<Exchange>();
    const auto force = [state, reply](ApiResponse::Kind kind, const QString &error) {
        if (state->forced) {
            return;
        }
        state->forced = true;
        state->forcedKind = kind;
        state->forcedError = error;
        reply->abort();
    };
    QObject::connect(reply, &QIODevice::readyRead, reply, [reply, state, force, limits] {
        state->body += reply->readAll();
        if (state->body.size() > limits.maxBytes) {
            force(ApiResponse::Kind::TooLarge, QStringLiteral("The answer was larger than expected."));
        }
    });
    auto *deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    QObject::connect(deadline, &QTimer::timeout, reply,
                     [force] { force(ApiResponse::Kind::Timeout, QStringLiteral("The server did not answer in time.")); });
    deadline->start(limits.timeoutMs);

    const QPointer<QObject> guard(context);
    const QString host = request.url().host();
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, state, guard, done, secret, host] {
        if (state->delivered) {
            return;
        }
        state->delivered = true;
        reply->deleteLater();
        ApiResponse response;
        response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (state->forced) {
            response.kind = state->forcedKind;
            response.error = state->forcedError;
        } else {
            state->body += reply->readAll();
            QJsonParseError parseError;
            response.json = QJsonDocument::fromJson(state->body, &parseError);
            const bool isJson = parseError.error == QJsonParseError::NoError;
            if (response.status >= 200 && response.status < 300) {
                response.kind = isJson ? ApiResponse::Kind::Ok : ApiResponse::Kind::BadJson;
                if (!isJson) {
                    response.error = QStringLiteral("%1 answered with something that is not JSON.").arg(host);
                }
            } else if (response.status == 401 || response.status == 403) {
                response.kind = ApiResponse::Kind::Unauthorized;
            } else if (response.status > 0) {
                response.kind = ApiResponse::Kind::HttpError;
            } else if (reply->error() == QNetworkReply::OperationCanceledError) {
                response.kind = ApiResponse::Kind::Timeout;
                response.error = QStringLiteral("The server did not answer in time.");
            } else {
                response.kind = ApiResponse::Kind::Network;
                response.error = QStringLiteral("Could not reach %1 (%2).").arg(host, reply->errorString());
            }
            if (response.status > 0 && response.error.isEmpty() && response.kind != ApiResponse::Kind::Ok) {
                response.error = QStringLiteral("%1 answered HTTP %2.").arg(host).arg(response.status);
            }
        }
        response.error = redact(response.error, secret);
        if (guard) {
            done(response);
        }
    });
}

} // namespace QindaOffice::Books::Connect
