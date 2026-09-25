// SPDX-License-Identifier: GPL-3.0-or-later
// End to end against local servers (127.0.0.1 only): a POP3 account syncs
// through the service (password from the keyring), a message sent over
// SMTP is filed in the local Sent folder and its draft goes away, a wrong
// password is retried once and then asks for sign-in, and a send to an
// unreachable server waits in the Outbox. No secret appears in a result,
// the store file or the account file.

#include "fake_mail_server.h"
#include "mail_test_support.h"

#include "qindamail/src/folder_model.h"

#include <qindatk/local_productivity_store.h>

#include <QFile>
#include <QJsonDocument>
#include <QTcpServer>

using namespace QindaOffice::Apps::Mail;

class TestMailFlow : public QObject {
    Q_OBJECT

    static QVariantMap pop3Account(quint16 popPort, quint16 smtpPort)
    {
        const auto server = [](quint16 port, bool pop) {
            QVariantMap map{{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                            {QStringLiteral("port"), port},
                            {QStringLiteral("security"), QStringLiteral("none")},
                            {QStringLiteral("userName"), QStringLiteral("ada")}};
            if (pop) map.insert(QStringLiteral("protocol"), QStringLiteral("pop3"));
            return map;
        };
        return {{QStringLiteral("kind"), QStringLiteral("pop3")},
                {QStringLiteral("displayName"), QStringLiteral("Home")},
                {QStringLiteral("emailAddress"), QStringLiteral("ada@home.example")},
                {QStringLiteral("fullName"), QStringLiteral("Ada Lovelace")},
                {QStringLiteral("incoming"), server(popPort, true)},
                {QStringLiteral("outgoing"), server(smtpPort, false)}};
    }

    static QByteArray sample()
    {
        return "From: Grace <grace@example.org>\r\nTo: ada@home.example\r\nSubject: Compiler news\r\n"
               "Message-ID: <news-1@example.org>\r\nDate: Fri, 25 Sep 2026 08:00:00 +0000\r\n\r\n"
               "It works.\r\n";
    }

private slots:
    void syncSendAndFile()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeMailServer pop(MailTest::FakeMailServer::Kind::Pop3, "s3cret-pop");
        MailTest::FakeMailServer smtp(MailTest::FakeMailServer::Kind::Smtp, "s3cret-pop");
        pop.mailbox.append(sample());
        const QVariantMap added = service->addAccount(pop3Account(pop.port(), smtp.port()), QStringLiteral("s3cret-pop"));
        QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(added.value(QStringLiteral("error")).toString()));
        const QString id = added.value(QStringLiteral("id")).toString();
        QTRY_VERIFY(env.secrets->contains(id + QStringLiteral("/pop3")));

        QSignalSpy finished(service, &MailService::operationFinished);
        const QString sync = service->syncAccount(id);
        const auto state = MailTest::waitState(finished, sync);
        QVERIFY2(state.first == QLatin1String("accepted"), qPrintable(state.second));
        QCOMPARE(pop.logins, 1);
        const QString inbox = QStringLiteral("acct:") + id + QStringLiteral(":INBOX");
        QTRY_COMPARE(service->folderModel()->rows().at(service->folderModel()->indexOfKey(inbox)).unread, 1);
        QCOMPARE(service->accountById(id).value(QStringLiteral("status")).toString(), QStringLiteral("idle"));
        QVERIFY(!service->accountById(id).value(QStringLiteral("lastSync")).toString().isEmpty());
        service->setCurrentFolderKey(inbox);
        QCOMPARE(service->messageModel()->rowCount(), 1);
        const QString messageId = service->messageModel()->get(0).value(QStringLiteral("messageId")).toString();
        QCOMPARE(service->messageById(messageId).value(QStringLiteral("accountId")).toString(), id);

        // POP3 read/flag are local edits: no second login.
        const QString read = service->markRead({messageId}, true);
        QCOMPARE(MailTest::waitState(finished, read).first, QStringLiteral("accepted"));
        QCOMPARE(pop.logins, 1);

        // Reply from the receiving account; delivered, filed locally.
        QVariantMap reply = service->replyDraft(messageId, false);
        QCOMPARE(reply.value(QStringLiteral("accountId")).toString(), id);
        QCOMPARE(reply.value(QStringLiteral("inReplyTo")).toString(), QStringLiteral("news-1@example.org"));
        QVERIFY(service->saveDraft(reply).value(QStringLiteral("ok")).toBool());
        QSignalSpy sent(service, &MailService::sendFinished);
        const QString request = service->send(reply);
        const QVariantMap result = MailTest::waitResult(sent, request);
        QVERIFY2(result.value(QStringLiteral("state")).toString() == QLatin1String("accepted"),
                 qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("sentCopy")).toString(), QStringLiteral("local"));
        QCOMPARE(smtp.delivered.size(), 1);
        QVERIFY(smtp.delivered.first().contains("Subject: Re: Compiler news"));
        QVERIFY(smtp.delivered.first().contains("In-Reply-To: <news-1@example.org>"));
        QVERIFY(service->store()->drafts().isEmpty());
        QTRY_COMPARE(service->folderModel()->rows().at(service->folderModel()->indexOfKey(
                                                           QStringLiteral("acct:local:Sent"))).total, 1);

        // No password anywhere it should not be.
        for (const QString &path : {env.options().dataDir + QStringLiteral("/store.json"), env.options().accountsPath}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY2(!file.readAll().contains("s3cret-pop"), qPrintable(path));
        }
        QVERIFY(!QVariant(result).toString().contains(QStringLiteral("s3cret")));
        QVERIFY(!QJsonDocument::fromVariant(result).toJson().contains("s3cret"));
    }

    void testAccountSavesNothing()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeMailServer pop(MailTest::FakeMailServer::Kind::Pop3, "dialog-pw");
        pop.mailbox.append(sample());
        QSignalSpy tested(service, &MailService::accountTestFinished);
        service->testAccount(QStringLiteral("t1"), pop3Account(pop.port(), pop.port()), QStringLiteral("dialog-pw"));
        QVariantMap result = MailTest::waitResult(tested, QStringLiteral("t1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("steps")).toList().size(), 1);
        QCOMPARE(pop.logins, 1);
        service->testAccount(QStringLiteral("t2"), pop3Account(pop.port(), pop.port()), QStringLiteral("nope"));
        result = MailTest::waitResult(tested, QStringLiteral("t2"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("needsSignIn")).toBool());
        // Nothing kept: no account, no secret, no message.
        QCOMPARE(service->accounts().size(), 1);
        QVERIFY(env.secrets->keys().isEmpty());
        QVERIFY(service->store()->messages().isEmpty());
        QVERIFY(!QJsonDocument::fromVariant(result).toJson().contains("dialog-pw"));
        service->testAccount(QStringLiteral("t3"), {{QStringLiteral("kind"), QStringLiteral("imap")}}, QString());
        QCOMPARE(MailTest::waitResult(tested, QStringLiteral("t3")).value(QStringLiteral("state")).toString(),
                 QStringLiteral("rejected"));
    }

    void wrongPasswordRetriesOnceThenNeedsSignIn()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeMailServer pop(MailTest::FakeMailServer::Kind::Pop3, "right");
        const QVariantMap added = service->addAccount(pop3Account(pop.port(), pop.port()), QStringLiteral("wrong"));
        const QString id = added.value(QStringLiteral("id")).toString();
        QTRY_VERIFY(env.secrets->contains(id + QStringLiteral("/pop3")));
        QSignalSpy finished(service, &MailService::operationFinished);
        const QString sync = service->syncFolder(QStringLiteral("acct:") + id + QStringLiteral(":INBOX"));
        QCOMPARE(MailTest::waitState(finished, sync).first, QStringLiteral("rejected"));
        QCOMPARE(pop.logins, 2);
        QCOMPARE(service->accountById(id).value(QStringLiteral("status")).toString(), QStringLiteral("needsSignIn"));

        // The right password (Account Settings) clears it on the next sync.
        QVariantMap account = service->accountStore()->accountById(id);
        const QVariantMap updated = service->updateAccount(account, QStringLiteral("right"));
        QVERIFY(updated.value(QStringLiteral("ok")).toBool());
        QTest::qWait(50);
        const QString again = service->syncFolder(QStringLiteral("acct:") + id + QStringLiteral(":INBOX"));
        QCOMPARE(MailTest::waitState(finished, again).first, QStringLiteral("accepted"));
        QCOMPARE(service->accountById(id).value(QStringLiteral("status")).toString(), QStringLiteral("idle"));
    }

    void unreachableSendWaitsInOutbox()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        const quint16 closed = probe.serverPort();
        probe.close();
        const QVariantMap added = service->addAccount(pop3Account(closed, closed), QStringLiteral("pw"));
        const QString id = added.value(QStringLiteral("id")).toString();
        QTRY_VERIFY(env.secrets->contains(id + QStringLiteral("/pop3")));
        QVariantMap draft = service->newDraft(id);
        draft.insert(QStringLiteral("to"), QStringLiteral("bob@example.org"));
        draft.insert(QStringLiteral("subject"), QStringLiteral("Later"));
        QSignalSpy sent(service, &MailService::sendFinished);
        const QString request = service->send(draft);
        const QVariantMap result = MailTest::waitResult(sent, request);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("queued")).toBool());
        QTRY_COMPARE(service->folderModel()->rows().at(service->folderModel()->indexOfKey(
                                                           QStringLiteral("acct:local:Outbox"))).total, 1);
        QCOMPARE(service->folderModel()->rows().at(service->folderModel()->indexOfKey(
                                                       QStringLiteral("acct:local:Drafts"))).total, 0);
    }
};

MAIL_TEST_MAIN(TestMailFlow)

#include "tst_qindamail_flow.moc"
