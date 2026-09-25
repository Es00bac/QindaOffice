// SPDX-License-Identifier: GPL-3.0-or-later
// OAuth2Session against a local fake authorization/token server (M43). The
// test plays the browser: it reads the authorization URL the session
// "opened" and GETs the loopback redirect with a code and state itself.

#include "oauth_harness.h"
#include "secret_sink.h"
#include "xoauth2.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace QindaOffice::Mail;
using namespace QindaOffice::Mail::Test;

namespace {

QStringList capturedLog;
QtMessageHandler previousHandler = nullptr;

void captureMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    capturedLog.append(message);
    if (previousHandler) previousHandler(type, context, message);
}

} // namespace

class TestMailOAuth : public QObject {
    Q_OBJECT

private:
    static QByteArray serialized(const QVariantMap &result)
    {
        return QJsonDocument(QJsonObject::fromVariantMap(result)).toJson();
    }

    static void verifyNoToken(const QByteArray &text)
    {
        for (const QString &secret : {kMailToken, kCalendarToken, kRefreshToken, kRotatedToken}) {
            QVERIFY2(!text.contains(secret.toUtf8()), qPrintable(QStringLiteral("leaked ") + secret));
        }
    }

private slots:
    void pkceMatchesRfc7636AppendixB()
    {
        QCOMPARE(Pkce::challengeS256("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
                 QByteArray("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
        const QByteArray v1 = Pkce::generateVerifier();
        const QByteArray v2 = Pkce::generateVerifier();
        QCOMPARE(v1.size(), 43);
        QVERIFY(v1 != v2);
        static const QRegularExpression unreserved(QStringLiteral("^[A-Za-z0-9._~-]{43,128}$"));
        QVERIFY(unreserved.match(QString::fromLatin1(v1)).hasMatch());
        QVERIFY(Pkce::generateState() != Pkce::generateState());
        QCOMPARE(Pkce::base64Url(QByteArray("\xfb\xff", 2)), QByteArray("-_8"));
    }

    void authorizationRequest()
    {
        Harness h(QStringLiteral("microsoft"));
        h.session.setLoginHint(QStringLiteral("bo@contoso.com"));
        QSignalSpy authorizing(&h.session, &OAuth2Session::authorizingChanged);
        h.session.authorize(QStringLiteral("a"));
        QVERIFY(h.session.authorizing());
        QCOMPARE(h.session.authorizationUrl(), h.opened);
        QCOMPARE(h.opened.path(), QStringLiteral("/authorize"));
        QCOMPARE(h.openedParam(QStringLiteral("response_type")), QStringLiteral("code"));
        QCOMPARE(h.openedParam(QStringLiteral("client_id")), QStringLiteral("client-123"));
        QCOMPARE(h.openedParam(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
        QCOMPARE(h.openedParam(QStringLiteral("code_challenge")).size(), 43);
        QCOMPARE(h.openedParam(QStringLiteral("login_hint")), QStringLiteral("bo@contoso.com"));
        QVERIFY(!h.openedParam(QStringLiteral("state")).isEmpty());
        QVERIFY(h.opened.toString(QUrl::FullyEncoded).indexOf(QStringLiteral("code_verifier")) < 0);
        const QUrl redirect(h.openedParam(QStringLiteral("redirect_uri")));
        QCOMPARE(redirect.scheme(), QStringLiteral("http"));
        QCOMPARE(redirect.host(), QStringLiteral("127.0.0.1"));
        QVERIFY(redirect.port() > 0);
        QCOMPARE(redirect.path(), QStringLiteral("/"));
        // Consent to both Microsoft resources in one authorization.
        const QStringList scopes = h.openedParam(QStringLiteral("scope")).split(QLatin1Char(' '));
        for (const char *s : {"offline_access", "https://outlook.office.com/IMAP.AccessAsUser.All",
                              "https://outlook.office.com/SMTP.Send", "Calendars.ReadWrite"}) {
            QVERIFY2(scopes.contains(QLatin1String(s)), s);
        }
        // A second authorize while one waits is refused, the first is kept.
        QSignalSpy finished(&h.session, &OAuth2Session::authorizeFinished);
        h.session.authorize(QStringLiteral("b"));
        QVERIFY(finished.wait(2000));
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("b"));
        QVERIFY(h.session.authorizing());
        h.session.cancel();
        QCOMPARE(finished.size(), 2);
        QVERIFY(finished.at(1).at(1).toMap().value(QStringLiteral("cancelled")).toBool());
        QVERIFY(!h.session.authorizing());
    }

    void fullFlowMicrosoft()
    {
        Harness h(QStringLiteral("microsoft"));
        const QVariantMap result = h.signIn();
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(h.lastStatus, 200);
        QVERIFY(h.lastPage.contains("close this tab"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
        QCOMPARE(result.value(QStringLiteral("resource")).toString(), QStringLiteral("mail"));
        QVERIFY(result.value(QStringLiteral("refreshTokenStored")).toBool());
        QVERIFY(result.value(QStringLiteral("hasRefreshToken")).toBool());
        QVERIFY(!result.value(QStringLiteral("expiresAt")).toString().isEmpty());
        verifyNoToken(serialized(result));

        QCOMPARE(h.session.accessToken(QStringLiteral("mail")), kMailToken);
        QVERIFY(h.session.hasValidToken());
        QVERIFY(h.session.expiresAt() > QDateTime::currentDateTimeUtc().addSecs(3000));
        QVERIFY(h.session.accessToken(QStringLiteral("calendar")).isEmpty()); // one token per resource
        QVERIFY(h.backend->contains(QStringLiteral("acc-1/oauth-refresh-microsoft")));

        const QList<FakeRequest> requests = h.tokenRequests();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.at(0).form(QStringLiteral("scope")),
                 QStringLiteral("offline_access https://outlook.office.com/IMAP.AccessAsUser.All "
                                "https://outlook.office.com/SMTP.Send"));
        QVERIFY(requests.at(0).form(QStringLiteral("client_secret")).isEmpty());

        // The loopback port is closed once the redirect arrived.
        int status = 0;
        QUrlQuery again;
        again.addQueryItem(QStringLiteral("code"), QStringLiteral("x"));
        h.browse(again, &status);
        QCOMPARE(status, 0);
    }

    void secondResourceSameRefreshToken()
    {
        Harness h(QStringLiteral("microsoft"));
        QVERIFY(h.signIn().value(QStringLiteral("ok")).toBool());
        h.rotate = true;
        const QVariantMap result = h.token(QStringLiteral("cal"), QStringLiteral("calendar"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("resource")).toString(), QStringLiteral("calendar"));
        const FakeRequest refresh = h.tokenRequests().last();
        QCOMPARE(refresh.form(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));
        QCOMPARE(refresh.form(QStringLiteral("refresh_token")), kRefreshToken); // '+' '/' '=' survived the form
        QCOMPARE(refresh.form(QStringLiteral("scope")), QStringLiteral("offline_access Calendars.ReadWrite"));
        QCOMPARE(h.session.accessToken(QStringLiteral("calendar")), kCalendarToken);
        QCOMPARE(h.session.accessToken(QStringLiteral("mail")), kMailToken);
        // The rotated refresh token replaced the stored one.
        QVERIFY(result.value(QStringLiteral("refreshTokenStored")).toBool());
        QSignalSpy read(&h.secrets, &MailSecrets::finished);
        SecretSink sink;
        h.secrets.applySecret(QStringLiteral("r"), QStringLiteral("acc-1"), QStringLiteral("oauth-refresh-microsoft"),
                              &sink, QStringLiteral("x"));
        QVERIFY(read.wait(2000));
        QCOMPARE(sink.received, kRotatedToken);
    }

    void googleOneTokenAndClientSecret()
    {
        Harness h(QStringLiteral("google"));
        h.session.setClientSecret(QStringLiteral("GOCSPX-not-confidential"));
        const QVariantMap result = h.signIn();
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(h.openedParam(QStringLiteral("access_type")), QStringLiteral("offline"));
        QCOMPARE(h.openedParam(QStringLiteral("prompt")), QStringLiteral("consent"));
        QVERIFY(h.openedParam(QStringLiteral("scope")).contains(QStringLiteral("https://mail.google.com/")));
        QVERIFY(h.openedParam(QStringLiteral("scope")).contains(QStringLiteral("https://www.googleapis.com/auth/calendar")));
        const FakeRequest exchange = h.tokenRequests().constFirst();
        QCOMPARE(exchange.form(QStringLiteral("client_secret")), QStringLiteral("GOCSPX-not-confidential"));
        QVERIFY(exchange.form(QStringLiteral("scope")).isEmpty());
        QCOMPARE(h.session.accessToken(QStringLiteral("calendar")), kMailToken);
        QVERIFY(h.backend->contains(QStringLiteral("acc-1/oauth-refresh-google")));
        // The calendar token is already valid: no second grant.
        const QVariantMap cal = h.token(QStringLiteral("c"), QStringLiteral("calendar"));
        QVERIFY(cal.value(QStringLiteral("cached")).toBool());
        QCOMPARE(h.tokenRequests().size(), 1);
    }

    void stateMismatchRejected()
    {
        Harness h(QStringLiteral("microsoft"));
        const QVariantMap result = h.signIn(QStringLiteral("forged-state"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("state mismatch")));
        QVERIFY(h.lastPage.contains("did not match"));
        QVERIFY(h.tokenRequests().isEmpty()); // the code was never exchanged
        QVERIFY(h.session.accessToken().isEmpty());
        QVERIFY(!h.session.authorizing());
    }

    void deniedConsent()
    {
        Harness h(QStringLiteral("microsoft"));
        QSignalSpy finished(&h.session, &OAuth2Session::authorizeFinished);
        h.session.authorize(QStringLiteral("d"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("error"), QStringLiteral("access_denied"));
        q.addQueryItem(QStringLiteral("error_description"), QStringLiteral("<script>alert(1)</script>"));
        q.addQueryItem(QStringLiteral("state"), h.openedParam(QStringLiteral("state")));
        const QByteArray page = h.browse(q);
        QVERIFY(finished.size() == 1 || finished.wait(2000));
        QVERIFY(finished.at(0).at(1).toMap().value(QStringLiteral("error")).toString().contains(QStringLiteral("declined")));
        QVERIFY(!page.contains("<script>")); // nothing reflected
    }

    void refreshFromKeyringThenCached()
    {
        Harness h(QStringLiteral("microsoft"));
        QSignalSpy wrote(&h.secrets, &MailSecrets::finished);
        h.secrets.write(QStringLiteral("w"), QStringLiteral("acc-1"), QStringLiteral("oauth-refresh-microsoft"), kRefreshToken);
        QVERIFY(wrote.wait(2000));
        QVERIFY(!h.session.hasRefreshToken());

        const QVariantMap first = h.token(QStringLiteral("t1"), QStringLiteral("mail"));
        QVERIFY2(first.value(QStringLiteral("ok")).toBool(), qPrintable(first.value(QStringLiteral("error")).toString()));
        QVERIFY(!first.value(QStringLiteral("cached")).toBool());
        QVERIFY(h.session.hasRefreshToken());
        const QVariantMap second = h.token(QStringLiteral("t2"), QStringLiteral("mail"));
        QVERIFY(second.value(QStringLiteral("cached")).toBool());
        QCOMPARE(h.tokenRequests().size(), 1);
        // A forced refresh asks again.
        QVERIFY(h.token(QStringLiteral("t3"), QStringLiteral("mail"), true).value(QStringLiteral("ok")).toBool());
        QCOMPARE(h.tokenRequests().size(), 2);
    }

    void concurrentRefreshesShareOneGrant()
    {
        Harness h(QStringLiteral("microsoft"));
        h.session.setRefreshToken(kRefreshToken);
        QSignalSpy finished(&h.session, &OAuth2Session::tokenFinished);
        h.session.ensureToken(QStringLiteral("x"), QStringLiteral("mail"));
        h.session.ensureToken(QStringLiteral("y"), QStringLiteral("mail"));
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("x"));
        QCOMPARE(finished.at(1).at(0).toString(), QStringLiteral("y"));
        QCOMPARE(finished.at(1).at(1).toMap().value(QStringLiteral("requestId")).toString(), QStringLiteral("y"));
        QCOMPARE(h.tokenRequests().size(), 1);
    }

    void invalidGrantNeedsReauthorization()
    {
        Harness h(QStringLiteral("microsoft"));
        QVERIFY(h.signIn().value(QStringLiteral("ok")).toBool());
        h.refreshAnswer = [](const FakeRequest &) {
            return json({{QStringLiteral("error"), QStringLiteral("invalid_grant")},
                         {QStringLiteral("error_description"), QStringLiteral("AADSTS70008: The refresh token has expired.")}},
                        400);
        };
        const QVariantMap result = h.token(QStringLiteral("r"), QStringLiteral("mail"), true);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("reauthorizeRequired")).toBool());
        QVERIFY(!result.value(QStringLiteral("retryable")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("AADSTS70008")));
        QVERIFY(!h.session.hasRefreshToken());
        QVERIFY(h.session.accessToken().isEmpty());
        QTRY_VERIFY(!h.backend->contains(QStringLiteral("acc-1/oauth-refresh-microsoft"))); // dead token wiped
        // With nothing saved, the next attempt says so without a request.
        const int before = int(h.tokenRequests().size());
        const QVariantMap next = h.token(QStringLiteral("r2"), QStringLiteral("mail"));
        QVERIFY(next.value(QStringLiteral("reauthorizeRequired")).toBool());
        QCOMPARE(int(h.tokenRequests().size()), before);
    }

    void serverTroubleIsOffline()
    {
        Harness h(QStringLiteral("microsoft"));
        h.session.setRefreshToken(kRefreshToken);
        h.refreshAnswer = [](const FakeRequest &) { return FakeResponse{503, "text/html", "<h1>busy</h1>", false}; };
        QVariantMap result = h.token(QStringLiteral("a"), QStringLiteral("mail"), true);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());
        QVERIFY(h.session.hasRefreshToken()); // not a reason to forget the sign-in
        h.refreshAnswer = [](const FakeRequest &) {
            return json({{QStringLiteral("error"), QStringLiteral("temporarily_unavailable")}}, 400);
        };
        result = h.token(QStringLiteral("b"), QStringLiteral("mail"), true);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        h.refreshAnswer = [](const FakeRequest &) {
            return json({{QStringLiteral("error"), QStringLiteral("invalid_client")}}, 401);
        };
        result = h.token(QStringLiteral("c"), QStringLiteral("mail"), true);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("client")));
        QVERIFY(!result.value(QStringLiteral("reauthorizeRequired")).toBool());
    }

    void timeoutAndCancel()
    {
        Harness h(QStringLiteral("microsoft"));
        h.session.setTimeoutMs(300);
        QSignalSpy finished(&h.session, &OAuth2Session::authorizeFinished);
        h.session.authorize(QStringLiteral("t"));
        QVERIFY(finished.wait(3000));
        QVariantMap result = finished.at(0).at(1).toMap();
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("did not finish")));
        QVERIFY(!h.session.authorizing());

        h.session.setTimeoutMs(60000);
        h.session.authorize(QStringLiteral("c"));
        h.session.cancel();
        QCOMPARE(finished.size(), 2);
        result = finished.at(1).at(1).toMap();
        QVERIFY(result.value(QStringLiteral("cancelled")).toBool());
        QVERIFY(!result.value(QStringLiteral("retryable")).toBool());
    }

    void tokensNeverLogged()
    {
        capturedLog.clear();
        QLoggingCategory::setFilterRules(QStringLiteral("*.debug=true"));
        previousHandler = qInstallMessageHandler(captureMessage);
        QByteArray results;
        {
            Harness h(QStringLiteral("microsoft"));
            results += serialized(h.signIn());
            h.rotate = true;
            results += serialized(h.token(QStringLiteral("c"), QStringLiteral("calendar")));
            results += serialized(h.token(QStringLiteral("m"), QStringLiteral("mail"), true));
            h.refreshAnswer = [](const FakeRequest &) {
                return json({{QStringLiteral("error"), QStringLiteral("invalid_grant")}}, 400);
            };
            results += serialized(h.token(QStringLiteral("x"), QStringLiteral("mail"), true));
            results += h.session.lastError().toUtf8();
        }
        qInstallMessageHandler(previousHandler);
        QLoggingCategory::setFilterRules(QString());
        verifyNoToken(capturedLog.join(QLatin1Char('\n')).toUtf8());
        verifyNoToken(results);
    }

    void applyAccessToken()
    {
        Harness h(QStringLiteral("microsoft"));
        SecretSink sink;
        QVERIFY(!h.session.applyAccessToken(&sink, QStringLiteral("xoauth2")));
        QVERIFY(h.signIn().value(QStringLiteral("ok")).toBool());
        QVERIFY(h.session.applyAccessToken(&sink, QStringLiteral("xoauth2")));
        QCOMPARE(sink.received, kMailToken);
        QCOMPARE(sink.receivedMechanism, QStringLiteral("xoauth2"));
        QVERIFY(!h.session.applyAccessToken(&sink, QStringLiteral("bearer"), QStringLiteral("calendar")));
        h.session.signOut();
        QVERIFY(!h.session.hasValidToken());
    }

    void configureFromRegistry()
    {
        QTemporaryDir dir;
        OAuthClientRegistry registry;
        registry.setUserPath(dir.filePath(QStringLiteral("oauth-clients.json")));
        registry.setSystemPath(dir.filePath(QStringLiteral("none.json")));
        OAuth2Session session;
        session.setRegistry(registry);
        const QVariantMap account{
            {QStringLiteral("id"), QStringLiteral("acc-9")},
            {QStringLiteral("kind"), QStringLiteral("microsoft365")},
            {QStringLiteral("emailAddress"), QStringLiteral("bo@contoso.com")},
            {QStringLiteral("oauth"), QVariantMap{{QStringLiteral("provider"), QStringLiteral("microsoft")}}},
            {QStringLiteral("calendar"), QVariantMap{{QStringLiteral("protocol"), QStringLiteral("graph")},
                                                     {QStringLiteral("authMechanism"), QStringLiteral("bearer")}}},
        };
        QVariantMap result = session.configure(account);
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("No OAuth client ID")));
        QCOMPARE(session.lastError(), result.value(QStringLiteral("error")).toString());

        QFile file(registry.userPath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"microsoft":{"clientId":"00000000-aaaa-bbbb-cccc-000000000000","tenant":"organizations"}})");
        file.close();
        result = session.configure(account);
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("source")).toString(), QStringLiteral("user"));
        QCOMPARE(session.provider(), QStringLiteral("microsoft"));
        QCOMPARE(session.clientId(), QStringLiteral("00000000-aaaa-bbbb-cccc-000000000000"));
        QCOMPARE(session.tenant(), QStringLiteral("organizations"));
        QCOMPARE(session.accountId(), QStringLiteral("acc-9"));
        QCOMPARE(session.loginHint(), QStringLiteral("bo@contoso.com"));
        QCOMPARE(session.resources(), (QStringList{QStringLiteral("mail"), QStringLiteral("calendar")}));
        const QVariantMap noOAuth{{QStringLiteral("kind"), QStringLiteral("imap")}};
        QVERIFY(!session.configure(noOAuth).value(QStringLiteral("ok")).toBool());
        // Real endpoints are https and carry the tenant.
        const OAuthProfile microsoft = *OAuthProfile::forProvider(QStringLiteral("microsoft"));
        QCOMPARE(microsoft.tokenUrl(QStringLiteral("organizations")).toString(),
                 QStringLiteral("https://login.microsoftonline.com/organizations/oauth2/v2.0/token"));
        QCOMPARE(microsoft.authorizationUrl({}).toString(),
                 QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/authorize"));
        const OAuthProfile google = *OAuthProfile::forProvider(QStringLiteral("google"));
        QCOMPARE(google.authorizationUrl({}).toString(), QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
        QCOMPARE(google.tokenUrl({}).toString(), QStringLiteral("https://oauth2.googleapis.com/token"));
    }

    void xoauth2Format()
    {
        // The example from Google's XOAUTH2 protocol page.
        QCOMPARE(XOAuth2::initialResponse(QStringLiteral("someuser@example.com"),
                                          QStringLiteral("ya29.vF9dft4qmTc2Nvb3RlckBhdHRhdmlzdGEuY29tCg")),
                 QByteArray("dXNlcj1zb21ldXNlckBleGFtcGxlLmNvbQFhdXRoPUJlYXJlciB5YTI5LnZGOWRmdDRxbVRjMk52YjNSbGNr"
                            "QmhkSFJoZG1semRHRXVZMjl0Q2cBAQ=="));
        // The same bytes the toolkit providers build from (userName, token).
        QCOMPARE(XOAuth2::rawInitialResponse(QStringLiteral("u@x"), QStringLiteral("t")),
                 QByteArray("user=u@x\x01" "auth=Bearer t\x01\x01"));
        const QJsonObject challenge = XOAuth2::decodeErrorChallenge(
            "eyJzdGF0dXMiOiI0MDEiLCJzY2hlbWVzIjoiYmVhcmVyIG1hYyIsInNjb3BlIjoiaHR0cHM6Ly9tYWlsLmdvb2dsZS5jb20vIn0K");
        QCOMPARE(challenge.value(QStringLiteral("status")).toString(), QStringLiteral("401"));
        QCOMPARE(challenge.value(QStringLiteral("scope")).toString(), QStringLiteral("https://mail.google.com/"));
        QVERIFY(XOAuth2::decodeErrorChallenge("not base64 !!").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMailOAuth)
#include "tst_mail_oauth.moc"
