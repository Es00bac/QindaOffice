// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts in the service: add/remove (keyring secrets written and removed,
// cached records purged, drafts kept), the protocol objects each account
// kind is built into (host, port, security, user, mechanism, secret key,
// OAuth resource), credential application before a request (a password
// reaches the provider; a missing one is "needsSignIn" without a
// connection) and the auth-rejection rule.

#include "mail_test_support.h"

#include "qindamail/src/account_runtime.h"
#include "qindamail/src/folder_model.h"

#include "ews_calendar_provider.h"
#include "graph_calendar_provider.h"

#include <qindatk/caldav_provider.h>
#include <qindatk/imap_mail_provider.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/pop3_mail_provider.h>
#include <qindatk/smtp_mail_provider.h>

#include <QFile>
#include <QTcpServer>

using namespace QindaOffice::Apps::Mail;

class TestMailAccounts : public QObject {
    Q_OBJECT

    static QVariantMap imapAccount(const QString &host = QStringLiteral("imap.example.org"), int port = 993)
    {
        return {{QStringLiteral("kind"), QStringLiteral("imap")},
                {QStringLiteral("emailAddress"), QStringLiteral("ada@example.org")},
                {QStringLiteral("fullName"), QStringLiteral("Ada Lovelace")},
                {QStringLiteral("incoming"), QVariantMap{{QStringLiteral("protocol"), QStringLiteral("imap")},
                                                         {QStringLiteral("host"), host},
                                                         {QStringLiteral("port"), port},
                                                         {QStringLiteral("security"), QStringLiteral("tls")}}},
                {QStringLiteral("outgoing"), QVariantMap{{QStringLiteral("host"), QStringLiteral("smtp.example.org")},
                                                         {QStringLiteral("port"), 587},
                                                         {QStringLiteral("security"), QStringLiteral("starttls")},
                                                         {QStringLiteral("sameCredentialsAsIncoming"), false},
                                                         {QStringLiteral("userName"), QStringLiteral("ada-smtp")}}},
                {QStringLiteral("specialFolders"), QVariantMap{{QStringLiteral("sent"), QStringLiteral("Sent")},
                                                               {QStringLiteral("trash"), QStringLiteral("Trash")}}}};
    }

private slots:
    void addAndRemoveAccount()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QSignalSpy changed(service, &MailService::accountsChanged);
        const QVariantMap added = service->addAccount(imapAccount(), QStringLiteral("hunter2-secret"));
        QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(added.value(QStringLiteral("error")).toString()));
        const QString id = added.value(QStringLiteral("id")).toString();
        QVERIFY(changed.count() > 0);
        QTRY_VERIFY(env.secrets->contains(id + QStringLiteral("/imap")));
        QVERIFY(env.secrets->contains(id + QStringLiteral("/smtp")));
        // Configuration on disk, the password only in the keyring.
        QFile file(env.options().accountsPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("hunter2-secret"));

        QCOMPARE(service->accounts().size(), 2);
        QCOMPARE(service->accounts().first().toMap().value(QStringLiteral("id")).toString(), id);
        QCOMPARE(service->defaultAccountId(), id);
        QCOMPARE(service->ownAddresses(), QStringList{QStringLiteral("ada@example.org")});
        QCOMPARE(service->sendableAccounts().size(), 1);
        QTRY_VERIFY(service->folderModel()->indexOfKey(QStringLiteral("acct:") + id + QStringLiteral(":INBOX")) > 0);
        // The first mail account's inbox is where a new window starts.
        QCOMPARE(env.reopen()->currentFolderKey(), QStringLiteral("acct:") + id + QStringLiteral(":INBOX"));
        // Special folders from the account settings appear before a LIST.
        QVERIFY(service->folderModel()->indexOfKey(QStringLiteral("acct:") + id + QStringLiteral(":Sent")) > 0);

        // Cached mail of the account, and a draft written from it.
        auto *store = service->store();
        store->upsert(QStringLiteral("messages"),
                      MailTest::message(QStringLiteral("imap:") + id + QStringLiteral(":INBOX:7:1"), QStringLiteral("Hi"),
                                        QDateTime::currentDateTimeUtc(),
                                        {{QStringLiteral("remoteAccount"), id},
                                         {QStringLiteral("remoteMailbox"), QStringLiteral("INBOX")}}));
        store->upsert(QStringLiteral("messages"),
                      MailTest::message(QStringLiteral("imap:") + id + QStringLiteral(":Work:7:2"), QStringLiteral("Job"),
                                        QDateTime::currentDateTimeUtc(),
                                        {{QStringLiteral("remoteAccount"), id},
                                         {QStringLiteral("remoteMailbox"), QStringLiteral("Work/Projects")}}));
        store->upsert(QStringLiteral("messages"),
                      MailTest::message(QStringLiteral("local:keep"), QStringLiteral("Mine"), QDateTime::currentDateTimeUtc()));
        store->upsert(QStringLiteral("drafts"), {{QStringLiteral("id"), QStringLiteral("draft:1")},
                                                 {QStringLiteral("accountId"), id},
                                                 {QStringLiteral("subject"), QStringLiteral("Unsent")}});
        QVERIFY(store->save());
        QTRY_COMPARE(service->unreadTotal(), 2);
        // A folder only the records know is listed (and its parent path kept).
        const int work = service->folderModel()->indexOfKey(QStringLiteral("acct:") + id + QStringLiteral(":Work/Projects"));
        QVERIFY(work > 0);
        QCOMPARE(service->folderModel()->rows().at(work).path, QStringLiteral("Work/Projects"));

        service->setCurrentFolderKey(QStringLiteral("acct:") + id + QStringLiteral(":INBOX"));
        const QVariantMap removed = service->removeAccount(id);
        QVERIFY(removed.value(QStringLiteral("ok")).toBool());
        QCOMPARE(removed.value(QStringLiteral("purgedRecords")).toInt(), 2);
        QTRY_VERIFY(!env.secrets->contains(id + QStringLiteral("/imap")));
        QVERIFY(!env.secrets->contains(id + QStringLiteral("/smtp")));
        QCOMPARE(service->accounts().size(), 1);
        QCOMPARE(store->messages().size(), 1);
        QCOMPARE(store->drafts().size(), 1);
        QVERIFY(store->drafts().first().toMap().value(QStringLiteral("accountId")).toString().isEmpty());
        QCOMPARE(service->currentFolderKey(), QStringLiteral("acct:local:Inbox"));
        QVERIFY(service->runtime(id) == nullptr);
    }

    void invalidAccountIsRefused()
    {
        MailTest::Env env;
        QVariantMap account = imapAccount();
        account.insert(QStringLiteral("password"), QStringLiteral("x"));
        const QVariantMap result = env.service->addAccount(account, QString());
        QVERIFY(!result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(env.service->accounts().size(), 1);
    }

    void providersFromPresets_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("host");
        QTest::addColumn<int>("port");
        QTest::addColumn<QString>("security");
        QTest::addColumn<QString>("mechanism");
        QTest::addColumn<QString>("smtpHost");
        QTest::addColumn<int>("smtpPort");
        QTest::addColumn<QString>("calendar");
        QTest::newRow("gmail") << "gmail" << "ada@gmail.com" << "imap.gmail.com" << 993 << "tls" << "xoauth2"
                               << "smtp.gmail.com" << 465 << "caldav";
        QTest::newRow("imap (yahoo)") << "imap" << "ada@yahoo.com" << "imap.mail.yahoo.com" << 993 << "tls" << "auto"
                                      << "smtp.mail.yahoo.com" << 465 << "";
        QTest::newRow("microsoft365") << "microsoft365" << "ada@contoso.example" << "outlook.office365.com" << 993
                                      << "tls" << "xoauth2" << "smtp.office365.com" << 587 << "graph";
    }

    void providersFromPresets()
    {
        QFETCH(QString, kind);
        QFETCH(QString, address);
        MailTest::Env env;
        MailService *service = env.service.get();
        const QVariantMap draft = service->accountStore()->draftFor(kind, address);
        const QVariantMap added = service->addAccount(draft);
        QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(added.value(QStringLiteral("error")).toString()));
        AccountRuntime *rt = service->runtime(added.value(QStringLiteral("id")).toString());
        QVERIFY(rt != nullptr);
        QVERIFY(rt->isImap());
        QindaTK::ImapMailProvider *imap = rt->imap(QStringLiteral("INBOX"));
        QTEST(imap->property("host").toString(), "host");
        QTEST(imap->property("port").toInt(), "port");
        QTEST(imap->property("security").toString(), "security");
        QCOMPARE(imap->property("userName").toString(), address);
        QCOMPARE(imap->property("accountId").toString(), rt->id());
        QCOMPARE(imap->property("mailbox").toString(), QStringLiteral("INBOX"));
        QTEST(rt->mechanismFor(QStringLiteral("imap")), "mechanism");
        QVERIFY(rt->imap(QStringLiteral("INBOX")) == imap);           // one per mailbox
        QVERIFY(rt->imap(QStringLiteral("Archive")) != imap);
        QindaTK::SmtpMailProvider *smtp = rt->smtp();
        QVERIFY(smtp != nullptr);
        QTEST(smtp->property("host").toString(), "smtpHost");
        QTEST(smtp->property("port").toInt(), "smtpPort");
        QCOMPARE(smtp->property("fromAddress").toString(), address);
        QCOMPARE(rt->secretServiceFor(QStringLiteral("smtp")), QStringLiteral("imap"));
        QFETCH(QString, mechanism);
        QCOMPARE(rt->usesOAuth(QStringLiteral("imap")), mechanism == QLatin1String("xoauth2"));
        QCOMPARE(rt->mechanismFor(QStringLiteral("smtp")), mechanism);
        QFETCH(QString, calendar);
        if (calendar.isEmpty()) {
            QVERIFY(rt->calendars().isEmpty());
        } else {
            QCOMPARE(rt->calendars().size(), 1);
            const CalendarBinding binding = rt->calendars().first();
            QCOMPARE(binding.service, calendar);
            QCOMPARE(binding.provider->property("accountId").toString(), rt->id());
            QCOMPARE(binding.provider->property("calendarId").toString(), binding.id);
            QCOMPARE(AccountRuntime::oauthResourceFor(calendar), QStringLiteral("calendar"));
            QCOMPARE(rt->mechanismFor(calendar), QStringLiteral("bearer"));
            if (calendar == QLatin1String("caldav")) {
                QVERIFY(qobject_cast<QindaTK::CalDavProvider *>(binding.provider) != nullptr);
                QCOMPARE(binding.provider->property("collectionUrl").toString(),
                         QStringLiteral("https://apidata.googleusercontent.com/caldav/v2/ada@gmail.com/events/"));
            } else {
                QVERIFY(qobject_cast<QindaOffice::Mail::GraphCalendarProvider *>(binding.provider) != nullptr);
            }
        }
        // Gmail and Microsoft 365 file sent mail themselves.
        QCOMPARE(rt->serverFilesSentMail(), kind != QLatin1String("imap"));
        // OAuth accounts without a configured client say so up front.
        if (rt->usesOAuth(QStringLiteral("imap")) && !rt->oauthConfigureError().isEmpty())
            QCOMPARE(rt->status(), QStringLiteral("needsSignIn"));
    }

    void pop3AndExchange()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        QVariantMap pop3 = service->accountStore()->draftFor(QStringLiteral("pop3"), QStringLiteral("bob@isp.example"));
        pop3.insert(QStringLiteral("incoming"), QVariantMap{{QStringLiteral("protocol"), QStringLiteral("pop3")},
                                                            {QStringLiteral("host"), QStringLiteral("pop.isp.example")},
                                                            {QStringLiteral("port"), 995},
                                                            {QStringLiteral("security"), QStringLiteral("tls")},
                                                            {QStringLiteral("authMechanism"), QStringLiteral("login")},
                                                            {QStringLiteral("leaveOnServer"), false}});
        pop3.insert(QStringLiteral("outgoing"), QVariantMap{{QStringLiteral("host"), QStringLiteral("smtp.isp.example")},
                                                            {QStringLiteral("port"), 465},
                                                            {QStringLiteral("security"), QStringLiteral("tls")}});
        const QVariantMap added = service->addAccount(pop3, QStringLiteral("pw"));
        QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(added.value(QStringLiteral("error")).toString()));
        AccountRuntime *rt = service->runtime(added.value(QStringLiteral("id")).toString());
        QVERIFY(rt->isPop3());
        QCOMPARE(rt->pop3()->property("host").toString(), QStringLiteral("pop.isp.example"));
        QCOMPARE(rt->pop3()->property("port").toInt(), 995);
        QCOMPARE(rt->pop3()->property("leaveOnServer").toBool(), false);
        QCOMPARE(rt->mechanismFor(QStringLiteral("pop3")), QStringLiteral("user"));
        QVERIFY(rt->imap(QStringLiteral("INBOX")) == nullptr);
        QVERIFY(!rt->serverFilesSentMail());
        const QString id = rt->id();
        QTRY_VERIFY(service->folderModel()->indexOfKey(QStringLiteral("acct:") + id + QStringLiteral(":Archive")) > 0);

        const QVariantMap exchange{
            {QStringLiteral("kind"), QStringLiteral("exchange")},
            {QStringLiteral("emailAddress"), QStringLiteral("cat@corp.example")},
            {QStringLiteral("calendar"),
             QVariantMap{{QStringLiteral("protocol"), QStringLiteral("ews")},
                         {QStringLiteral("url"), QStringLiteral("https://mail.corp.example/EWS/Exchange.asmx")},
                         {QStringLiteral("userName"), QStringLiteral("CORP\\cat")},
                         {QStringLiteral("authMechanism"), QStringLiteral("basic")}}}};
        const QVariantMap ews = service->addAccount(exchange, QStringLiteral("pw2"));
        QVERIFY2(ews.value(QStringLiteral("ok")).toBool(), qPrintable(ews.value(QStringLiteral("error")).toString()));
        AccountRuntime *ert = service->runtime(ews.value(QStringLiteral("id")).toString());
        QVERIFY(!ert->isImap() && !ert->isPop3() && !ert->canSend());
        QCOMPARE(ert->calendars().size(), 1);
        auto *provider = qobject_cast<QindaOffice::Mail::EwsCalendarProvider *>(ert->calendars().first().provider);
        QVERIFY(provider != nullptr);
        QCOMPARE(provider->property("url").toString(), QStringLiteral("https://mail.corp.example/EWS/Exchange.asmx"));
        QCOMPARE(provider->property("userName").toString(), QStringLiteral("CORP\\cat"));
        QCOMPARE(ert->mechanismFor(QStringLiteral("ews")), QStringLiteral("basic"));
        QTRY_VERIFY(env.secrets->contains(ert->id() + QStringLiteral("/ews")));
        // Its calendar is offered to the event dialog.
        bool listed = false;
        for (const QVariant &value : service->writableCalendars())
            listed = listed || value.toMap().value(QStringLiteral("id")).toString() == ert->calendars().first().id;
        QVERIFY(listed);
    }

    void credentialsBeforeEveryRequest()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        // A port on 127.0.0.1 nobody listens on: the request must get as far
        // as connecting (offline), which proves the secret was applied.
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        const int port = probe.serverPort();
        probe.close();
        QVariantMap account = imapAccount(QStringLiteral("127.0.0.1"), port);
        const QVariantMap added = service->addAccount(account, QStringLiteral("pw"));
        const QString id = added.value(QStringLiteral("id")).toString();
        QTRY_VERIFY(env.secrets->contains(id + QStringLiteral("/imap")));
        QSignalSpy finished(service, &MailService::operationFinished);
        QString request = service->syncFolder(QStringLiteral("acct:") + id + QStringLiteral(":INBOX"));
        QCOMPARE(MailTest::waitState(finished, request).first, QStringLiteral("offline"));
        QVERIFY(service->runtime(id)->imap(QStringLiteral("INBOX"))->hasSecret());
        QCOMPARE(service->accountById(id).value(QStringLiteral("status")).toString(), QStringLiteral("offline"));

        // Without a stored password nothing is sent: the account needs sign-in.
        const QVariantMap other = service->addAccount(imapAccount(QStringLiteral("127.0.0.1"), port));
        const QString otherId = other.value(QStringLiteral("id")).toString();
        request = service->syncAccount(otherId);
        QCOMPARE(MailTest::waitState(finished, request).first, QStringLiteral("rejected"));
        QCOMPARE(service->accountById(otherId).value(QStringLiteral("status")).toString(), QStringLiteral("needsSignIn"));
        QVERIFY(!service->runtime(otherId)->imap(QStringLiteral("INBOX"))->hasSecret());
        QVERIFY(!service->statusText().isEmpty());
    }

    void authRejectionRule_data()
    {
        QTest::addColumn<QString>("state");
        QTest::addColumn<QString>("error");
        QTest::addColumn<bool>("auth");
        QTest::newRow("imap") << "rejected" << "AUTHENTICATE failed: NO [AUTHENTICATIONFAILED] Invalid credentials" << true;
        QTest::newRow("pop3") << "rejected" << "PASS failed: -ERR [AUTH] bad" << true;
        QTest::newRow("smtp") << "rejected" << "Authentication refused: 535" << true;
        QTest::newRow("caldav") << "rejected" << "The calendar server refused the credentials (HTTP 401)" << true;
        QTest::newRow("tls policy") << "rejected" << "Refusing to send credentials over an unencrypted connection"
                                    << false;
        QTest::newRow("mailbox") << "rejected" << "SELECT failed: NO no such mailbox" << false;
        QTest::newRow("offline") << "offline" << "Connection refused" << false;
    }

    void authRejectionRule()
    {
        QFETCH(QString, state);
        QFETCH(QString, error);
        QTEST(AccountRuntime::isAuthRejection({{QStringLiteral("state"), state}, {QStringLiteral("error"), error}}),
              "auth");
    }
};

MAIL_TEST_MAIN(TestMailAccounts)

#include "tst_qindamail_accounts.moc"
