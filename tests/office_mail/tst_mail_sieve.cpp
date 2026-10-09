// SPDX-License-Identifier: GPL-3.0-or-later
// M81 (D-090): automatic replies over Sieve — QindaMail's two pieces in a
// user's script (placed after the requires, removed byte for byte, never
// touching the rest), scheduling with currentdate, and the ManageSieve
// client against a local fake server: read, turn on (CHECKSCRIPT then
// PUTSCRIPT into the active script), turn off (the user's script comes back
// unchanged), a mailbox without a script, LOGIN, refusals, an unsupported
// server, a refused connection and no secret in any answer.

#include "fake_managesieve_server.h"
#include "managesieve_provider.h"
#include "sieve_script.h"

#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>

using namespace QindaOffice::Mail;
using MailTest::FakeManageSieveServer;

namespace {

const QString kUserScript = QStringLiteral(
    "# Roundcube filters\r\n"
    "require [\"fileinto\", \"envelope\"];\r\n"
    "require \"date\"; # for the archive rule\r\n"
    "/* spam first */\r\n"
    "if header :contains \"subject\" \"[SPAM]\" {\r\n"
    "  fileinto \"Junk\";\r\n"
    "  stop;\r\n"
    "}\r\n"
    "if envelope :is \"from\" \"boss@example.org\" { fileinto \"Boss\"; }\r\n");

AutoReply reply(const QString &text, bool scheduled = false)
{
    AutoReply r;
    r.enabled = true;
    r.internalMessage = text;
    r.subject = QStringLiteral("Out of office");
    r.days = 3;
    r.scheduled = scheduled;
    r.start = QDateTime(QDate(2026, 10, 12), QTime(8, 0), QTimeZone::UTC);
    r.end = QDateTime(QDate(2026, 10, 23), QTime(17, 0), QTimeZone::UTC);
    return r;
}

QVariantMap waitFor(QSignalSpy &spy, const QString &requestId)
{
    for (int i = 0; i < 200; ++i) {
        for (const QList<QVariant> &args : std::as_const(spy))
            if (args.at(0).toString() == requestId) return args.at(1).toMap();
        spy.wait(50);
    }
    return {{QStringLiteral("state"), QStringLiteral("timeout")}};
}

ManageSieveProvider::Server serverFor(quint16 port)
{
    ManageSieveProvider::Server server;
    server.host = QStringLiteral("127.0.0.1");
    server.port = port;
    server.security = QStringLiteral("none");
    server.userName = QStringLiteral("ada@example.org");
    server.timeoutMs = 5000;
    return server;
}

} // namespace

class TestMailSieve : public QObject {
    Q_OBJECT
private slots:
    void pieces()
    {
        QCOMPARE(Sieve::quoted(QStringLiteral("say \"hi\" \\ now\nplease")), QStringLiteral("\"say \\\"hi\\\" \\\\ now please\""));
        QCOMPARE(Sieve::multiline(QStringLiteral("Hello\n.hidden\nBye"), QStringLiteral("\r\n")),
                 QStringLiteral("text:\r\nHello\r\n..hidden\r\nBye\r\n.\r\n"));
        // The leading requires end after "# for the archive rule\r\n".
        const qsizetype end = Sieve::requireSectionEnd(kUserScript);
        QVERIFY(kUserScript.left(end).endsWith(QStringLiteral("# for the archive rule\r\n")));
        QCOMPARE(Sieve::requireSectionEnd(QStringLiteral("if true { stop; }")), 0);
        QCOMPARE(Sieve::requireSectionEnd(QStringLiteral("require \"a;b\";\nkeep;\n")), 15);
        QCOMPARE(Sieve::requiredExtensions(reply(QStringLiteral("x"))), QStringList{QStringLiteral("vacation")});
        QCOMPARE(Sieve::requiredExtensions(reply(QStringLiteral("x"), true)).size(), 3);
    }

    void blockGoesAfterTheRequiresAndComesOut_data()
    {
        QTest::addColumn<QString>("script");
        QTest::newRow("roundcube-crlf") << kUserScript;
        QTest::newRow("lf") << QString(kUserScript).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        QTest::newRow("no-requires") << QStringLiteral("keep;\n");
        QTest::newRow("empty") << QString();
        QTest::newRow("only-comment") << QStringLiteral("# nothing yet\n");
    }

    void blockGoesAfterTheRequiresAndComesOut()
    {
        QFETCH(QString, script);
        const QStringList addresses{QStringLiteral("ada@example.org"), QStringLiteral("ada@work.example")};
        const QString on = Sieve::withVacation(script, reply(QStringLiteral("I am away.\nBack soon.")), addresses);
        QVERIFY(on.startsWith(QStringLiteral("require [\"vacation\"]; # QindaMail automatic reply")));
        QVERIFY(Sieve::hasManagedVacation(on));
        QVERIFY(!Sieve::hasForeignVacation(on));
        // The user's text is all there, in order, around the block.
        const qsizetype begin = on.indexOf(QLatin1String(Sieve::kBeginMarker));
        const qsizetype finish = on.indexOf(QLatin1String(Sieve::kEndMarker));
        QVERIFY(begin > 0 && finish > begin);
        QVERIFY(on.contains(QStringLiteral("vacation :days 3 :subject \"Out of office\" :addresses [\"ada@example.org\", \"ada@work.example\"] text:")));
        QVERIFY(on.contains(QStringLiteral("I am away.")));
        const QString head = script.left(Sieve::requireSectionEnd(script));
        QVERIFY(on.mid(on.indexOf(QLatin1Char('\n')) + 1).startsWith(head));
        // Off again: byte for byte the user's script.
        QCOMPARE(Sieve::withoutVacation(on), script);
        // On twice (new text): still one block, the new text.
        const QString again = Sieve::withVacation(on, reply(QStringLiteral("Different text")), addresses);
        QCOMPARE(again.count(QLatin1String(Sieve::kBeginMarker)), 1);
        QCOMPARE(again.count(QLatin1String(Sieve::kRequireMarker)), 1);
        QVERIFY(again.contains(QStringLiteral("Different text")) && !again.contains(QStringLiteral("I am away.")));
        QCOMPARE(Sieve::withoutVacation(again), script);
        bool found = false;
        const AutoReply read = Sieve::readManaged(again, &found);
        QVERIFY(found && read.enabled);
        QCOMPARE(read.internalMessage, QStringLiteral("Different text"));
        QCOMPARE(read.subject, QStringLiteral("Out of office"));
        QCOMPARE(read.days, 3);
        Sieve::readManaged(script, &found);
        QVERIFY(!found);
    }

    void scheduledBlock()
    {
        const QString on = Sieve::withVacation(kUserScript, reply(QStringLiteral("Away"), true), {}, QTimeZone(QByteArray("America/Denver")));
        QVERIFY(on.startsWith(QStringLiteral("require [\"vacation\", \"date\", \"relational\"]; # QindaMail")));
        QVERIFY2(on.contains(QStringLiteral("if allof (currentdate :zone \"-0600\" :value \"ge\" \"date\" \"2026-10-12\",")), qPrintable(on));
        QVERIFY(on.contains(QStringLiteral("currentdate :zone \"-0600\" :value \"le\" \"date\" \"2026-10-23\") {")));
        QCOMPARE(Sieve::withoutVacation(on), kUserScript);
        bool found = false;
        const AutoReply read = Sieve::readManaged(on, &found);
        QVERIFY(read.scheduled);
        QCOMPARE(read.start, QDateTime(QDate(2026, 10, 12), QTime(8, 0), QTimeZone::UTC));
        // Another program's vacation is noticed, not touched.
        const QString foreign = kUserScript + QStringLiteral("vacation :days 7 \"Gone fishing\";\r\n");
        QVERIFY(Sieve::hasForeignVacation(foreign));
        QVERIFY(Sieve::hasForeignVacation(Sieve::withVacation(foreign, reply(QStringLiteral("x")), {})));
        QVERIFY(!Sieve::hasForeignVacation(QStringLiteral("# my vacation rule\r\nkeep;\r\n")));
    }

    void readTurnOnTurnOff()
    {
        FakeManageSieveServer server("secret-pw");
        server.scripts.insert(QStringLiteral("roundcube"), kUserScript.toUtf8());
        server.scripts.insert(QStringLiteral("old"), "keep;\r\n");
        server.active = QStringLiteral("roundcube");
        ManageSieveProvider provider(serverFor(server.port()));
        provider.setSecret(QStringLiteral("secret-pw"), QStringLiteral("auto"));
        QSignalSpy spy(&provider, &ManageSieveProvider::vacationFinished);

        QVERIFY(provider.readVacation(QStringLiteral("r1")));
        QVariantMap result = waitFor(spy, QStringLiteral("r1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("activeScript")).toString(), QStringLiteral("roundcube"));
        QVERIFY(!result.value(QStringLiteral("settings")).toMap().value(QStringLiteral("enabled")).toBool());
        QVERIFY(result.value(QStringLiteral("available")).toBool());
        QVERIFY(result.value(QStringLiteral("extensions")).toStringList().contains(QStringLiteral("vacation")));

        const QStringList addresses{QStringLiteral("ada@example.org")};
        QVERIFY(provider.writeVacation(QStringLiteral("w1"), AutoReplies::toVariantMap(reply(QStringLiteral("Back on Monday."), true)), addresses));
        result = waitFor(spy, QStringLiteral("w1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QVERIFY(result.value(QStringLiteral("changed")).toBool());
        QCOMPARE(server.checks, 1);                                         // checked before the PUT
        const QString stored = QString::fromUtf8(server.scripts.value(QStringLiteral("roundcube")));
        QVERIFY(Sieve::hasManagedVacation(stored));
        QCOMPARE(Sieve::withoutVacation(stored), kUserScript);              // the user's filters intact
        QCOMPARE(server.active, QStringLiteral("roundcube"));
        QCOMPARE(server.scripts.value(QStringLiteral("old")), QByteArray("keep;\r\n"));

        QVERIFY(provider.readVacation(QStringLiteral("r2")));
        result = waitFor(spy, QStringLiteral("r2"));
        const QVariantMap settings = result.value(QStringLiteral("settings")).toMap();
        QVERIFY(settings.value(QStringLiteral("enabled")).toBool());
        QVERIFY(settings.value(QStringLiteral("scheduled")).toBool());
        QCOMPARE(settings.value(QStringLiteral("internalMessage")).toString(), QStringLiteral("Back on Monday."));

        AutoReply off = reply(QStringLiteral("Back on Monday."));
        off.enabled = false;
        QVERIFY(provider.writeVacation(QStringLiteral("w2"), AutoReplies::toVariantMap(off), addresses));
        result = waitFor(spy, QStringLiteral("w2"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(server.scripts.value(QStringLiteral("roundcube")), kUserScript.toUtf8());   // byte for byte
        // Nothing to change: no PUT.
        const int puts = int(std::count_if(server.commands.cbegin(), server.commands.cend(),
                                           [](const QByteArray &c) { return c.startsWith("PUTSCRIPT"); }));
        QVERIFY(provider.writeVacation(QStringLiteral("w3"), AutoReplies::toVariantMap(off), addresses));
        result = waitFor(spy, QStringLiteral("w3"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool() && !result.value(QStringLiteral("changed")).toBool());
        QCOMPARE(int(std::count_if(server.commands.cbegin(), server.commands.cend(),
                                   [](const QByteArray &c) { return c.startsWith("PUTSCRIPT"); })), puts);
        // No secret anywhere in what came back.
        for (const QList<QVariant> &args : std::as_const(spy)) {
            const QString text = QString::fromUtf8(QJsonDocument::fromVariant(args.at(1)).toJson());
            QVERIFY(!text.contains(QStringLiteral("secret-pw")));
            QVERIFY(!text.contains(QString::fromLatin1(QByteArray("\0ada@example.org\0secret-pw", 26).toBase64())));
        }
    }

    void mailboxWithoutAScript()
    {
        FakeManageSieveServer server("pw");
        server.sasl = "LOGIN";                                             // challenges, not PLAIN
        ManageSieveProvider provider(serverFor(server.port()));
        provider.setSecret(QStringLiteral("pw"));
        QSignalSpy spy(&provider, &ManageSieveProvider::vacationFinished);
        QVERIFY(provider.writeVacation(QStringLiteral("on"), AutoReplies::toVariantMap(reply(QStringLiteral("Away"))), {}));
        QVariantMap result = waitFor(spy, QStringLiteral("on"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(server.active, QString::fromLatin1(ManageSieveProvider::kScriptName));
        QVERIFY(Sieve::hasManagedVacation(QString::fromUtf8(server.scripts.value(server.active))));
        AutoReply off = reply(QStringLiteral("Away"));
        off.enabled = false;
        QVERIFY(provider.writeVacation(QStringLiteral("off"), AutoReplies::toVariantMap(off), {}));
        result = waitFor(spy, QStringLiteral("off"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(server.active.isEmpty());                                   // switched off …
        QVERIFY(server.scripts.isEmpty());                                  // … and QindaMail's script removed
    }

    void refusals()
    {
        // Wrong password: rejected, the account's sign-in problem.
        FakeManageSieveServer server("right");
        ManageSieveProvider provider(serverFor(server.port()));
        provider.setSecret(QStringLiteral("wrong"));
        QSignalSpy spy(&provider, &ManageSieveProvider::vacationFinished);
        QVERIFY(provider.readVacation(QStringLiteral("bad")));
        QVariantMap result = waitFor(spy, QStringLiteral("bad"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("AUTHENTICATE failed")));

        // A server without the vacation extension: not available, said plainly.
        FakeManageSieveServer plain("pw");
        plain.extensions = {QStringLiteral("fileinto")};
        ManageSieveProvider noVacation(serverFor(plain.port()));
        noVacation.setSecret(QStringLiteral("pw"));
        QSignalSpy spy2(&noVacation, &ManageSieveProvider::vacationFinished);
        QVERIFY(noVacation.readVacation(QStringLiteral("nv")));
        result = waitFor(spy2, QStringLiteral("nv"));
        QVERIFY(!result.value(QStringLiteral("available")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("vacation")));

        // A time range on a server without date/relational.
        FakeManageSieveServer old("pw");
        old.extensions = {QStringLiteral("vacation")};
        ManageSieveProvider noDate(serverFor(old.port()));
        noDate.setSecret(QStringLiteral("pw"));
        QSignalSpy spy3(&noDate, &ManageSieveProvider::vacationFinished);
        QVERIFY(noDate.writeVacation(QStringLiteral("nd"), AutoReplies::toVariantMap(reply(QStringLiteral("x"), true)), {}));
        result = waitFor(spy3, QStringLiteral("nd"));
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("time range")));
        QVERIFY(old.scripts.isEmpty());

        // STARTTLS asked for but not offered: nothing (no credentials) is sent.
        ManageSieveProvider::Server tls = serverFor(server.port());
        tls.security = QStringLiteral("starttls");
        ManageSieveProvider strict(tls);
        strict.setSecret(QStringLiteral("right"));
        QSignalSpy spy4(&strict, &ManageSieveProvider::vacationFinished);
        const int before = int(server.commands.size());
        QVERIFY(strict.readVacation(QStringLiteral("tls")));
        result = waitFor(spy4, QStringLiteral("tls"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("STARTTLS")));
        QCOMPARE(int(server.commands.size()), before);

        // Nobody listening: offline, retryable.
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        const quint16 port = closed.serverPort();
        closed.close();
        ManageSieveProvider nowhere(serverFor(port));
        nowhere.setSecret(QStringLiteral("pw"));
        QSignalSpy spy5(&nowhere, &ManageSieveProvider::vacationFinished);
        QVERIFY(nowhere.readVacation(QStringLiteral("off")));
        result = waitFor(spy5, QStringLiteral("off"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());

        // No secret / no host / invalid settings: refused on the next turn.
        ManageSieveProvider bare(serverFor(server.port()));
        QSignalSpy spy6(&bare, &ManageSieveProvider::vacationFinished);
        QVERIFY(!bare.readVacation(QStringLiteral("none")));
        QCOMPARE(spy6.count(), 0);
        QCOMPARE(waitFor(spy6, QStringLiteral("none")).value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        bare.setSecret(QStringLiteral("pw"));
        AutoReply empty = reply(QString());
        QVERIFY(!bare.writeVacation(QStringLiteral("empty"), AutoReplies::toVariantMap(empty), {}));
        QVERIFY(waitFor(spy6, QStringLiteral("empty")).value(QStringLiteral("error")).toString().contains(QStringLiteral("write the reply")));
    }
};

QTEST_GUILESS_MAIN(TestMailSieve)

#include "tst_mail_sieve.moc"
