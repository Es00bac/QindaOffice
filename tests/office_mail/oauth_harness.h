// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// tst_mail_oauth's harness: one OAuth2Session wired to a FakeHttpServer
// token endpoint and an in-memory keyring. The test plays the browser
// (browse()/signIn() GET the loopback redirect themselves).

#include "fake_http_server.h"
#include "mail_secrets.h"
#include "oauth2_session.h"
#include "pkce.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QUrlQuery>

namespace QindaOffice::Mail::Test {

// Distinctive values, so a leak anywhere is found by a plain substring search.
inline const QString kCode = QStringLiteral("code-4/0Ab+x/yz");
inline const QString kMailToken = QStringLiteral("AT-mail-7f3a9c0e11");
inline const QString kCalendarToken = QStringLiteral("AT-cal-b5d10e22");
inline const QString kRefreshToken = QStringLiteral("RT-one-51b2e8+/=");
inline const QString kRotatedToken = QStringLiteral("RT-two-9e4c1f77");

inline FakeResponse json(const QJsonObject &object, int status = 200)
{
    return FakeResponse{status, "application/json", QJsonDocument(object).toJson(QJsonDocument::Compact), false};
}

// One session wired to a fake server and an in-memory keyring.
struct Harness {
    explicit Harness(const QString &provider)
    {
        session.setProvider(provider);
        session.setClientId(QStringLiteral("client-123"));
        session.setAccountId(QStringLiteral("acc-1"));
        session.setSecrets(&secrets);
        session.setEndpoints(server.url(QStringLiteral("/authorize")), server.url(QStringLiteral("/token")));
        session.setBrowserOpener([this](const QUrl &url) {
            opened = url;
            return true;
        });
        server.route(QStringLiteral("/token"), [this](const FakeRequest &r) { return token(r); });
    }

    FakeResponse token(const FakeRequest &r)
    {
        const QString grant = r.form(QStringLiteral("grant_type"));
        if (grant == QLatin1String("authorization_code")) {
            const QUrlQuery auth(opened);
            const QString challenge = auth.queryItemValue(QStringLiteral("code_challenge"), QUrl::FullyDecoded);
            const QByteArray verifier = r.form(QStringLiteral("code_verifier")).toLatin1();
            if (r.form(QStringLiteral("code")) != kCode || Pkce::challengeS256(verifier) != challenge.toLatin1()
                || r.form(QStringLiteral("redirect_uri")) != auth.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded)
                || r.form(QStringLiteral("client_id")) != QLatin1String("client-123")) {
                return json({{QStringLiteral("error"), QStringLiteral("invalid_grant")}}, 400);
            }
            return json({{QStringLiteral("access_token"), kMailToken}, {QStringLiteral("token_type"), QStringLiteral("Bearer")},
                         {QStringLiteral("expires_in"), 3599}, {QStringLiteral("refresh_token"), kRefreshToken},
                         {QStringLiteral("scope"), r.form(QStringLiteral("scope"))}});
        }
        if (grant == QLatin1String("refresh_token")) {
            if (refreshAnswer) return refreshAnswer(r);
            if (r.form(QStringLiteral("refresh_token")) != kRefreshToken) {
                return json({{QStringLiteral("error"), QStringLiteral("invalid_grant")}}, 400);
            }
            const bool calendar = r.form(QStringLiteral("scope")).contains(QStringLiteral("Calendars"));
            return json({{QStringLiteral("access_token"), calendar ? kCalendarToken : kMailToken},
                         {QStringLiteral("token_type"), QStringLiteral("Bearer")},
                         {QStringLiteral("expires_in"), QStringLiteral("3600")},
                         {QStringLiteral("refresh_token"), rotate ? kRotatedToken : kRefreshToken}});
        }
        return json({{QStringLiteral("error"), QStringLiteral("unsupported_grant_type")}}, 400);
    }

    [[nodiscard]] QList<FakeRequest> tokenRequests() const
    {
        QList<FakeRequest> list;
        for (const FakeRequest &r : server.requests()) {
            if (r.path == QLatin1String("/token")) list.append(r);
        }
        return list;
    }

    [[nodiscard]] QString openedParam(const QString &key) const
    {
        return QUrlQuery(opened).queryItemValue(key, QUrl::FullyDecoded);
    }

    // The browser: GET the loopback redirect. Returns the page (status in *status).
    QByteArray browse(const QUrlQuery &query, int *status = nullptr)
    {
        QUrl url(openedParam(QStringLiteral("redirect_uri")));
        url.setQuery(query);
        QNetworkReply *reply = browser.get(QNetworkRequest(url));
        QSignalSpy done(reply, &QNetworkReply::finished);
        done.wait(5000);
        if (status) *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        return body;
    }

    QVariantMap signIn(const QString &state = {})
    {
        QSignalSpy finished(&session, &OAuth2Session::authorizeFinished);
        session.authorize(QStringLiteral("auth-1"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("code"), QString::fromLatin1(QUrl::toPercentEncoding(kCode)));
        q.addQueryItem(QStringLiteral("state"), state.isEmpty() ? openedParam(QStringLiteral("state")) : state);
        lastPage = browse(q, &lastStatus);
        if (finished.isEmpty() && !finished.wait(5000)) return {};
        return finished.at(0).at(1).toMap();
    }

    QVariantMap token(const QString &requestId, const QString &resource, bool force = false)
    {
        QSignalSpy finished(&session, &OAuth2Session::tokenFinished);
        if (force) session.refresh(requestId, resource);
        else session.ensureToken(requestId, resource);
        if (!finished.wait(5000)) return {};
        return finished.at(0).at(1).toMap();
    }

    FakeHttpServer server;
    std::shared_ptr<InMemorySecretBackend> backend = std::make_shared<InMemorySecretBackend>();
    MailSecrets secrets{backend};
    OAuth2Session session;
    QNetworkAccessManager browser;
    QUrl opened;
    QByteArray lastPage;
    int lastStatus = 0;
    bool rotate = false;
    std::function<FakeResponse(const FakeRequest &)> refreshAnswer;
};

} // namespace QindaOffice::Mail::Test
