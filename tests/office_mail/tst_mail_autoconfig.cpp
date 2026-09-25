// SPDX-License-Identifier: GPL-3.0-or-later
// AutoConfig: the Thunderbird clientConfig parser over real-world-shaped
// documents, and the lookup order against a local fake server (M43).

#include "autoconfig.h"
#include "client_config_parser.h"
#include "fake_http_server.h"

#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

using namespace QindaOffice::Mail;
using QindaOffice::Mail::Test::FakeHttpServer;
using QindaOffice::Mail::Test::FakeResponse;

namespace {

QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(QQO_MAIL_FIXTURES "/") + QString::fromLatin1(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

FakeResponse xml(const QByteArray &body)
{
    return FakeResponse{200, "text/xml", body, false};
}

} // namespace

class TestMailAutoConfig : public QObject {
    Q_OBJECT

private:
    // Points the three network steps at `server` (paths /ac, /wk, /ispdb).
    static void useServer(AutoConfig &config, const FakeHttpServer &server)
    {
        // Built as text: QUrl would percent-encode the placeholders.
        const QString base = QStringLiteral("http://127.0.0.1:") + QString::number(server.serverPort());
        config.setEndpoints({base + QStringLiteral("/ac/%DOMAIN%?emailaddress=%EMAILADDRESS%"),
                             base + QStringLiteral("/wk/%DOMAIN%"), base + QStringLiteral("/ispdb/%DOMAIN%")});
        config.setUsePresets(false);
    }

    static QVariantMap run(AutoConfig &config, const QString &address, int timeoutMs = 10000)
    {
        QSignalSpy spy(&config, &AutoConfig::finished);
        config.lookup(QStringLiteral("r1"), address);
        if (!spy.wait(timeoutMs)) return {};
        return spy.at(0).at(1).toMap();
    }

    static QStringList outcomes(const QVariantMap &result)
    {
        QStringList list;
        for (const QVariant &a : result.value(QStringLiteral("attempts")).toList()) {
            const QVariantMap m = a.toMap();
            list << m.value(QStringLiteral("source")).toString() + QLatin1Char(':') + m.value(QStringLiteral("outcome")).toString();
        }
        return list;
    }

private slots:
    void parseGmail()
    {
        const ClientConfig config = ClientConfigParser::parse(fixture("gmail.xml"), QStringLiteral("ada@gmail.com"));
        QVERIFY2(config.ok, qPrintable(config.error));
        QCOMPARE(config.providerId, QStringLiteral("googlemail.com"));
        QCOMPARE(config.displayName, QStringLiteral("Google Mail"));
        QCOMPARE(config.domains.size(), 3);
        QCOMPARE(config.incoming.size(), 2);
        QCOMPARE(config.incoming.at(0).type, QStringLiteral("imap"));
        QCOMPARE(config.incoming.at(0).host, QStringLiteral("imap.gmail.com"));
        QCOMPARE(config.incoming.at(0).port, 993);
        QCOMPARE(config.incoming.at(0).security, QStringLiteral("tls"));
        QCOMPARE(config.incoming.at(0).userName, QStringLiteral("ada@gmail.com"));
        QCOMPARE(config.incoming.at(0).authentication,
                 (QStringList{QStringLiteral("OAuth2"), QStringLiteral("password-cleartext")}));
        QCOMPARE(config.incoming.at(1).type, QStringLiteral("pop3"));
        QCOMPARE(config.outgoing.at(0).port, 465);
        QCOMPARE(config.documentationUrl, QStringLiteral("http://mail.google.com/support/bin/answer.py?answer=13273"));
        // A Google host becomes a full gmail account (OAuth, calendar, folders).
        bool ok = false;
        const MailAccount account = config.toAccount(QStringLiteral("ada@gmail.com"), &ok);
        QVERIFY(ok);
        QCOMPARE(account.kind, QStringLiteral("gmail"));
        QCOMPARE(account.oauth.provider, QStringLiteral("google"));
        QCOMPARE(account.calendar.protocol, QStringLiteral("caldav"));
        QCOMPARE(account.specialFolders.sent, QStringLiteral("[Gmail]/Sent Mail"));
    }

    void parseOutlook()
    {
        const ClientConfig config = ClientConfigParser::parse(fixture("outlook.xml"), QStringLiteral("bo@hotmail.com"));
        QVERIFY2(config.ok, qPrintable(config.error));
        // The exchange server type is not ours and is dropped.
        QCOMPARE(config.incoming.size(), 2);
        QCOMPARE(config.incoming.at(0).type, QStringLiteral("imap"));
        QCOMPARE(config.outgoing.at(0).host, QStringLiteral("smtp.office365.com"));
        QCOMPARE(config.outgoing.at(0).security, QStringLiteral("starttls"));
        bool ok = false;
        const MailAccount account = config.toAccount(QStringLiteral("bo@hotmail.com"), &ok);
        QVERIFY(ok);
        QCOMPARE(account.kind, QStringLiteral("microsoft365"));
        QCOMPARE(account.incoming.authMechanism, QStringLiteral("xoauth2"));
        QCOMPARE(account.outgoing.host, QStringLiteral("smtp.office365.com")); // discovered beats preset
        QCOMPARE(account.calendar.protocol, QStringLiteral("graph"));
    }

    void parseFastmail()
    {
        const ClientConfig config = ClientConfigParser::parse(fixture("fastmail.xml"), QStringLiteral("cy@fastmail.fm"));
        QVERIFY2(config.ok, qPrintable(config.error));
        bool ok = false;
        const MailAccount account = config.toAccount(QStringLiteral("cy@fastmail.fm"), &ok);
        QVERIFY(ok);
        QCOMPARE(account.kind, QStringLiteral("imap"));
        QCOMPARE(account.incoming.host, QStringLiteral("imap.fastmail.com"));
        QCOMPARE(account.incoming.authMechanism, QStringLiteral("auto"));
        QCOMPARE(account.outgoing.port, 465);
        QCOMPARE(account.outgoing.security, QStringLiteral("tls"));
        QVERIFY(account.oauth.provider.isEmpty());
    }

    void parsePlaceholdersAndSkipPlainText()
    {
        const ClientConfig config = ClientConfigParser::parse(fixture("regional-isp.xml"), QStringLiteral("dee@example.net"));
        QVERIFY2(config.ok, qPrintable(config.error));
        QCOMPARE(config.incoming.at(0).security, QStringLiteral("none"));
        QCOMPARE(config.incoming.at(0).host, QStringLiteral("mail.example.net"));
        QCOMPARE(config.incoming.at(0).userName, QStringLiteral("dee"));
        bool ok = false;
        const MailAccount account = config.toAccount(QStringLiteral("dee@example.net"), &ok);
        QVERIFY(ok);
        QCOMPARE(account.incoming.security, QStringLiteral("starttls")); // the plain one was skipped
        QCOMPARE(account.incoming.userName, QStringLiteral("dee"));
        QCOMPARE(account.outgoing.host, QStringLiteral("smtp.example.net"));
    }

    void parseRejectsGarbage_data()
    {
        QTest::addColumn<QByteArray>("document");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("html") << QByteArray("<html><body>Not found</body></html>");
        QTest::newRow("broken") << QByteArray("<clientConfig><emailProvider><incomingServer type=\"imap\">");
        QTest::newRow("no servers") << QByteArray("<clientConfig><emailProvider id=\"x\"/></clientConfig>");
        QTest::newRow("huge") << QByteArray("<clientConfig>") + QByteArray(ClientConfigParser::kMaxBytes, ' ');
    }

    void parseRejectsGarbage()
    {
        QFETCH(QByteArray, document);
        const ClientConfig config = ClientConfigParser::parse(document, QStringLiteral("a@example.org"));
        QVERIFY(!config.ok);
        QVERIFY(!config.error.isEmpty());
    }

    void presetNeedsNoNetwork()
    {
        FakeHttpServer server;
        AutoConfig config;
        useServer(config, server);
        config.setUsePresets(true);
        const QVariantMap result = run(config, QStringLiteral("ada@icloud.com"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
        QCOMPARE(result.value(QStringLiteral("source")).toString(), QStringLiteral("preset"));
        QCOMPARE(result.value(QStringLiteral("presetId")).toString(), QStringLiteral("icloud"));
        QCOMPARE(result.value(QStringLiteral("auth")).toString(), QStringLiteral("app-password"));
        QCOMPARE(result.value(QStringLiteral("account")).toMap().value(QStringLiteral("incoming")).toMap()
                     .value(QStringLiteral("host")).toString(),
                 QStringLiteral("imap.mail.me.com"));
        QVERIFY(server.requests().isEmpty());
    }

    void fallbackOrder()
    {
        FakeHttpServer server;
        server.route(QStringLiteral("/ispdb/example.org"), xml(fixture("fastmail.xml")));
        AutoConfig config;
        useServer(config, server);
        config.setUsePresets(true); // example.org has no preset: recorded as not-found
        const QVariantMap result = run(config, QStringLiteral("eve@example.org"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
        QCOMPARE(result.value(QStringLiteral("source")).toString(), QStringLiteral("ispdb"));
        QCOMPARE(outcomes(result), (QStringList{QStringLiteral("preset:not-found"), QStringLiteral("autoconfig:not-found"),
                                                QStringLiteral("well-known:not-found"), QStringLiteral("ispdb:found")}));
        QCOMPARE(server.requestPaths(), (QStringList{QStringLiteral("/ac/example.org"), QStringLiteral("/wk/example.org"),
                                                     QStringLiteral("/ispdb/example.org")}));
        // Only the provider's own autoconfig server sees the address.
        QCOMPARE(server.requests().at(0).query.queryItemValue(QStringLiteral("emailaddress"), QUrl::FullyDecoded),
                 QStringLiteral("eve@example.org"));
        QVERIFY(!server.requests().at(2).query.hasQueryItem(QStringLiteral("emailaddress")));
        QCOMPARE(result.value(QStringLiteral("incoming")).toList().size(), 2);
    }

    void firstSourceWins()
    {
        FakeHttpServer server;
        server.route(QStringLiteral("/ac/example.net"), xml(fixture("regional-isp.xml")));
        server.route(QStringLiteral("/ispdb/example.net"), xml(fixture("fastmail.xml")));
        AutoConfig config;
        useServer(config, server);
        const QVariantMap result = run(config, QStringLiteral("dee@example.net"));
        QCOMPARE(result.value(QStringLiteral("source")).toString(), QStringLiteral("autoconfig"));
        QCOMPARE(server.requests().size(), 1);
        QCOMPARE(result.value(QStringLiteral("account")).toMap().value(QStringLiteral("incoming")).toMap()
                     .value(QStringLiteral("host")).toString(),
                 QStringLiteral("mail.example.net"));
    }

    void invalidDocumentFallsThrough()
    {
        FakeHttpServer server;
        server.route(QStringLiteral("/ac/example.org"), xml("<html>captive portal</html>"));
        server.route(QStringLiteral("/wk/example.org"), xml(fixture("fastmail.xml")));
        AutoConfig config;
        useServer(config, server);
        const QVariantMap result = run(config, QStringLiteral("eve@example.org"));
        QCOMPARE(result.value(QStringLiteral("source")).toString(), QStringLiteral("well-known"));
        QCOMPARE(outcomes(result), (QStringList{QStringLiteral("autoconfig:invalid"), QStringLiteral("well-known:found")}));
    }

    void nothingFound()
    {
        FakeHttpServer server;
        AutoConfig config;
        useServer(config, server);
        const QVariantMap result = run(config, QStringLiteral("eve@example.org"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(!result.value(QStringLiteral("retryable")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("example.org")));
        QCOMPARE(result.value(QStringLiteral("source")).toString(), QString());
    }

    void unreachableIsOffline()
    {
        quint16 closedPort = 0;
        {
            QTcpServer probe;
            QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
            closedPort = probe.serverPort();
        }
        const QString base = QStringLiteral("http://127.0.0.1:") + QString::number(closedPort);
        AutoConfig config;
        config.setUsePresets(false);
        config.setEndpoints({base + QStringLiteral("/ac/%DOMAIN%"), base + QStringLiteral("/wk/%DOMAIN%"),
                             base + QStringLiteral("/ispdb/%DOMAIN%")});
        const QVariantMap result = run(config, QStringLiteral("eve@example.org"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());
    }

    void httpsOnly()
    {
        AutoConfig config;
        config.setUsePresets(false);
        // Plain http to a non-loopback host is refused before connecting.
        config.setEndpoints({QStringLiteral("http://autoconfig.%DOMAIN%/mail/config-v1.1.xml"),
                             QStringLiteral("http://%DOMAIN%/.well-known/autoconfig/mail/config-v1.1.xml"),
                             QStringLiteral("ftp://autoconfig.thunderbird.net/%DOMAIN%")});
        const QVariantMap result = run(config, QStringLiteral("eve@example.org"));
        QCOMPARE(outcomes(result), (QStringList{QStringLiteral("autoconfig:refused"), QStringLiteral("well-known:refused"),
                                                QStringLiteral("ispdb:refused")}));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QCOMPARE(AutoConfig::defaultEndpoints().ispdb, QStringLiteral("https://autoconfig.thunderbird.net/v1.1/%DOMAIN%"));
        QVERIFY(AutoConfig::defaultEndpoints().autoconfig.startsWith(QStringLiteral("https://autoconfig.%DOMAIN%/")));
        QVERIFY(AutoConfig::defaultEndpoints().wellKnown.startsWith(QStringLiteral("https://%DOMAIN%/.well-known/")));
    }

    void oversizedAndSlowAnswers()
    {
        FakeHttpServer server;
        server.route(QStringLiteral("/ac/example.org"),
                     xml(QByteArray("<clientConfig>") + QByteArray(ClientConfigParser::kMaxBytes + 10, ' ')));
        server.route(QStringLiteral("/wk/example.org"), FakeResponse{200, "text/xml", {}, true}); // hangs
        server.route(QStringLiteral("/ispdb/example.org"), xml(fixture("fastmail.xml")));
        AutoConfig config;
        useServer(config, server);
        config.setTimeoutMs(1000);
        const QVariantMap result = run(config, QStringLiteral("eve@example.org"));
        QCOMPARE(outcomes(result), (QStringList{QStringLiteral("autoconfig:invalid"), QStringLiteral("well-known:offline"),
                                                QStringLiteral("ispdb:found")}));
    }

    void cancelAndBadInput()
    {
        FakeHttpServer server;
        server.route(QStringLiteral("/ac/example.org"), FakeResponse{200, "text/xml", {}, true});
        AutoConfig config;
        useServer(config, server);
        QSignalSpy spy(&config, &AutoConfig::finished);
        config.lookup(QStringLiteral("c"), QStringLiteral("eve@example.org"));
        QVERIFY(config.busy());
        QTRY_COMPARE(server.requests().size(), 1);
        config.cancel(QStringLiteral("c"));
        QCOMPARE(spy.size(), 1);
        QVERIFY(spy.at(0).at(1).toMap().value(QStringLiteral("cancelled")).toBool());
        QVERIFY(!config.busy());

        config.lookup(QStringLiteral("bad"), QStringLiteral("not-an-address"));
        config.lookup(QStringLiteral("bad2"), QStringLiteral("x@exa mple.org/../"));
        QTRY_COMPARE(spy.size(), 3);
        QCOMPARE(spy.at(1).at(1).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QCOMPARE(spy.at(2).at(1).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QCOMPARE(server.requests().size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestMailAutoConfig)
#include "tst_mail_autoconfig.moc"
