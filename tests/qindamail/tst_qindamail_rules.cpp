// SPDX-License-Identifier: GPL-3.0-or-later
// M81 (D-090): Outlook's rules through QindaMail's service against local
// fake IMAP and SMTP servers on 127.0.0.1 — a mailbox's first download
// runs no rule, later arrivals do (move, mark read, category keyword,
// \Flagged), a header condition fetches the header block first, copy and
// permanent delete, forward and redirect through the outgoing queue (a
// redirect keeps the sender and adds a Resent block; offline it waits in
// the Outbox), the desktop alert, the order of rules with exceptions and
// "stop processing", Run Rules Now on a folder, Create Rule, import and
// export, Ignore and Clean Up Conversation.

#include "mail_test_support.h"

#include "fake_imap_server.h"
#include "fake_mail_server.h"

#include "qindamail/src/account_runtime.h"
#include "qindamail/src/mail_organizer.h"

#include "desktop_notifier.h"

#include <qindatk/local_productivity_store.h>

#include <QFile>
#include <QJsonDocument>

using namespace QindaOffice::Apps::Mail;

namespace QindaOffice::Apps::Mail {
// MailService's test door (it names this class a friend).
class MailServiceTestAccess {
public:
    static void setOnline(MailService *service, bool online) { service->setOnline(online); }
    static void flushOutbox(MailService *service) { service->flushOutbox(); }
};
} // namespace QindaOffice::Apps::Mail

namespace {

QByteArray mail(const QByteArray &from, const QByteArray &subject, const QByteArray &id, const QByteArray &body = "Hello there",
                const QByteArray &extra = {})
{
    return "From: " + from + "\r\nTo: Ada <ada@work.example>\r\nSubject: " + subject
        + "\r\nDate: Thu, 8 Oct 2026 09:00:00 +0000\r\nMessage-ID: <" + id + ">\r\n" + extra
        + "MIME-Version: 1.0\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n" + body + "\r\n";
}

QVariantMap condition(const QString &type, const QStringList &values = {}, const QString &value = {})
{
    QVariantMap c{{QStringLiteral("type"), type}};
    if (!values.isEmpty()) c.insert(QStringLiteral("values"), values);
    if (!value.isEmpty()) c.insert(QStringLiteral("value"), value);
    return c;
}

QVariantMap action(const QString &type, const QString &value = {}, const QString &folderKey = {}, const QStringList &values = {})
{
    QVariantMap a{{QStringLiteral("type"), type}};
    if (!value.isEmpty()) a.insert(QStringLiteral("value"), value);
    if (!folderKey.isEmpty()) a.insert(QStringLiteral("folderKey"), folderKey);
    if (!values.isEmpty()) a.insert(QStringLiteral("values"), values);
    return a;
}

QVariantMap rule(const QString &name, const QVariantList &conditions, const QVariantList &actions, const QVariantList &exceptions = {})
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("enabled"), true}, {QStringLiteral("conditions"), conditions},
            {QStringLiteral("actions"), actions}, {QStringLiteral("exceptions"), exceptions}};
}

// An IMAP account on the fake server (SMTP on its own fake, same password),
// with a Projects folder in its mailbox list.
struct ImapAccount {
    MailTest::FakeImapServer imap{"pw"};
    MailTest::FakeMailServer smtp{MailTest::FakeMailServer::Kind::Smtp, "pw"};
    QString id;

    bool add(MailService *service)
    {
        const auto server = [](quint16 port, bool incoming) {
            QVariantMap map{{QStringLiteral("host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), port},
                            {QStringLiteral("security"), QStringLiteral("none")}, {QStringLiteral("userName"), QStringLiteral("ada")}};
            if (incoming) map.insert(QStringLiteral("protocol"), QStringLiteral("imap"));
            return map;
        };
        const QVariantMap added = service->addAccount({{QStringLiteral("kind"), QStringLiteral("imap")},
                                                       {QStringLiteral("displayName"), QStringLiteral("Work")},
                                                       {QStringLiteral("emailAddress"), QStringLiteral("ada@work.example")},
                                                       {QStringLiteral("incoming"), server(imap.port(), true)},
                                                       {QStringLiteral("outgoing"), server(smtp.port(), false)}},
                                                      QStringLiteral("pw"));
        id = added.value(QStringLiteral("id")).toString();
        if (AccountRuntime *rt = service->runtime(id)) {
            rt->setMailboxes({QVariantMap{{QStringLiteral("name"), QStringLiteral("INBOX")}, {QStringLiteral("specialUse"), QStringLiteral("inbox")}},
                              QVariantMap{{QStringLiteral("name"), QStringLiteral("Projects")}, {QStringLiteral("delimiter"), QStringLiteral("/")}},
                              QVariantMap{{QStringLiteral("name"), QStringLiteral("Trash")}, {QStringLiteral("specialUse"), QStringLiteral("trash")}}});
        }
        return added.value(QStringLiteral("ok")).toBool();
    }
    QString key(const QString &mailbox) const { return QStringLiteral("acct:") + id + QLatin1Char(':') + mailbox; }
    QString record(qint64 uid) const { return QStringLiteral("imap:%1:INBOX:4242:%2").arg(id).arg(uid); }
};

QString sync(MailService *service, const QString &folderKey)
{
    QSignalSpy finished(service, &MailService::operationFinished);
    return MailTest::waitState(finished, service->syncFolder(folderKey)).first;
}

bool hasUid(const QList<MailTest::FakeImapServer::Message> &box, qint64 uid)
{
    return std::any_of(box.cbegin(), box.cend(), [uid](const MailTest::FakeImapServer::Message &m) { return m.uid == uid; });
}

QString importLocal(MailTest::Env &env, const QString &name, const QByteArray &bytes)
{
    const QString path = env.dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(bytes);
    file.close();
    return env.service->importMessageFile(path);
}

} // namespace

class TestMailRules : public QObject {
    Q_OBJECT
private slots:
    void arrivalsMeetTheRulesButNotTheFirstDownload()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        ImapAccount account;
        account.imap.inbox.append({1, mail("Grace <grace@navy.example>", "Old budget", "old@x"), false});
        QVERIFY(account.add(service));
        MailOrganizer *organizer = service->organizer();
        const QVariantMap saved = organizer->saveRule(rule(QStringLiteral("Budget mail"),
            {condition(QStringLiteral("from"), {QStringLiteral("grace@navy")})},
            {action(QStringLiteral("assignCategory"), {}, {}, {QStringLiteral("Finance")}), action(QStringLiteral("flag"), QStringLiteral("today")),
             action(QStringLiteral("markRead")), action(QStringLiteral("moveTo"), {}, account.key(QStringLiteral("Projects")))}));
        QVERIFY2(saved.value(QStringLiteral("ok")).toBool(), qPrintable(saved.value(QStringLiteral("errors")).toStringList().join('\n')));

        QCOMPARE(sync(service, account.key(QStringLiteral("INBOX"))), QStringLiteral("accepted"));
        QTest::qWait(300);
        QVERIFY(hasUid(account.imap.inbox, 1));                       // the first download stays where it is
        QVERIFY(!organizer->running());

        account.imap.inbox.append({2, mail("Grace <grace@navy.example>", "Budget review", "new@x"), false});
        account.imap.inbox.append({3, mail("Linus <linus@kernel.example>", "Patch", "patch@x"), false});
        QCOMPARE(sync(service, account.key(QStringLiteral("INBOX"))), QStringLiteral("accepted"));
        QTRY_VERIFY_WITH_TIMEOUT(account.imap.folders.value(QStringLiteral("Projects")).size() == 1, 10000);
        const MailTest::FakeImapServer::Message moved = account.imap.folders.value(QStringLiteral("Projects")).first();
        QVERIFY(moved.source.contains("Budget review"));
        QVERIFY(moved.seen);
        QVERIFY(moved.flagged);
        QCOMPARE(moved.keywords, QStringList{QStringLiteral("Finance")});
        QVERIFY(!hasUid(account.imap.inbox, 2));
        QVERIFY(hasUid(account.imap.inbox, 3));                       // not Grace's: untouched
        QVERIFY(account.imap.commands.contains("UID STORE 2 +FLAGS.SILENT (Finance)"));
        QVERIFY(account.imap.commands.contains("UID MOVE 2 \"Projects\""));
        QTRY_VERIFY(!organizer->running());
        QVERIFY(service->messageById(account.record(3)).value(QStringLiteral("unread")).toBool());
    }

    void headersCopyAndPermanentDelete()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        ImapAccount account;
        QVERIFY(account.add(service));
        QCOMPARE(sync(service, account.key(QStringLiteral("INBOX"))), QStringLiteral("accepted"));   // empty: the baseline
        MailOrganizer *organizer = service->organizer();
        QVERIFY(organizer->saveRule(rule(QStringLiteral("Newsletter"), {condition(QStringLiteral("header"), {QStringLiteral("List-Id: <news")})},
            {action(QStringLiteral("copyTo"), {}, account.key(QStringLiteral("Projects"))), action(QStringLiteral("deletePermanently"))})).value(QStringLiteral("ok")).toBool());
        account.imap.inbox.append({5, mail("News <news@shop.example>", "Deals", "deal@x", "Buy", "List-Id: <news.shop.example>\r\n"), false});
        account.imap.inbox.append({6, mail("News <news@shop.example>", "Hello", "hello@x"), false});
        QCOMPARE(sync(service, account.key(QStringLiteral("INBOX"))), QStringLiteral("accepted"));
        QTRY_VERIFY2_WITH_TIMEOUT(!hasUid(account.imap.inbox, 5), account.imap.commands.join(" | ").constData(), 10000);
        QVERIFY(account.imap.commands.contains("UID FETCH 5 (UID BODY.PEEK[HEADER])"));
        QCOMPARE(account.imap.folders.value(QStringLiteral("Projects")).size(), 1);   // the copy
        QVERIFY(account.imap.folders.value(QStringLiteral("Trash")).isEmpty());       // expunged, not trashed
        QVERIFY(account.imap.commands.contains("UID COPY 5 \"Projects\""));
        QTRY_VERIFY(!organizer->running());
        QVERIFY(hasUid(account.imap.inbox, 6));                                         // no List-Id
    }

    void forwardRedirectAndAlert()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        ImapAccount account;
        QVERIFY(account.add(service));
        QCOMPARE(sync(service, account.key(QStringLiteral("INBOX"))), QStringLiteral("accepted"));
        QStringList alerts;
        QindaOffice::Mail::DesktopNotifier::setSink([&alerts](const QString &title, const QString &body) { alerts << title + QLatin1Char('|') + body; });
        MailOrganizer *organizer = service->organizer();
        QVERIFY(organizer->saveRule(rule(QStringLiteral("Hand on"), {condition(QStringLiteral("subject"), {QStringLiteral("invoice")})},
            {action(QStringLiteral("forward"), QStringLiteral("Bob <bob@example.org>")),
             action(QStringLiteral("redirect"), QStringLiteral("carol@example.org")),
             action(QStringLiteral("forward"), QStringLiteral("billing@acme.example")),    // the sender: no loop
             action(QStringLiteral("notify"), QStringLiteral("Invoice arrived"))})).value(QStringLiteral("ok")).toBool());
        account.imap.inbox.append({9, mail("Billing <billing@acme.example>", "Invoice INV-7", "inv7@acme.example", "Amount due: 12"), false});
        QCOMPARE(sync(service, account.key(QStringLiteral("INBOX"))), QStringLiteral("accepted"));
        QTRY_VERIFY2_WITH_TIMEOUT(account.smtp.delivered.size() == 2,
                                  qPrintable(QString::number(account.smtp.delivered.size()) + QStringLiteral(" drafts: ")
                                             + QString::fromUtf8(QJsonDocument::fromVariant(service->store()->drafts()).toJson(QJsonDocument::Compact))),
                                  15000);
        QByteArray forwarded, redirected;
        for (const QByteArray &data : std::as_const(account.smtp.delivered))
            (data.contains("Resent-From:") ? redirected : forwarded) = data;
        QVERIFY2(forwarded.contains("Subject: Fwd: Invoice INV-7"), forwarded.constData());
        QVERIFY(forwarded.contains("bob@example.org"));
        QVERIFY(redirected.contains("Resent-From: <ada@work.example>"));
        QVERIFY(redirected.contains("Resent-To: <carol@example.org>"));
        QVERIFY(redirected.contains("From: Billing <billing@acme.example>"));       // the sender stays theirs
        QVERIFY(redirected.contains("Message-ID: <inv7@acme.example>"));
        QVERIFY(account.smtp.recipients.contains("<carol@example.org>"));
        QCOMPARE(alerts.size(), 1);
        QVERIFY(alerts.first().startsWith(QStringLiteral("Invoice arrived|Billing")));
        QTRY_VERIFY(service->store()->drafts().isEmpty());                         // both left the Outbox
        QindaOffice::Mail::DesktopNotifier::setSink({});
    }

    void offlineRedirectWaitsInTheOutbox()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        ImapAccount account;
        QVERIFY(account.add(service));
        const QString id = importLocal(env, QStringLiteral("a.eml"), mail("Grace <grace@navy.example>", "Plans", "plans@x"));
        QVERIFY(!id.isEmpty());
        MailOrganizer *organizer = service->organizer();
        const QString ruleId = organizer->saveRule(rule(QStringLiteral("Pass on"), {condition(QStringLiteral("from"), {QStringLiteral("grace")})},
                                                        {action(QStringLiteral("redirect"), QStringLiteral("carol@example.org"))})).value(QStringLiteral("id")).toString();
        MailServiceTestAccess::setOnline(service, false);
        QSignalSpy ran(organizer, &MailOrganizer::rulesRunFinished);
        const QString run = organizer->runRulesNow({ruleId}, QStringLiteral("acct:local:Inbox"), false, QStringLiteral("all"));
        QCOMPARE(MailTest::waitResult(ran, run).value(QStringLiteral("matched")).toInt(), 1);
        QCOMPARE(service->store()->drafts().size(), 1);
        const QVariantMap queued = service->store()->drafts().first().toMap();
        QVERIFY(queued.value(QStringLiteral("queued")).toBool());
        QCOMPARE(queued.value(QStringLiteral("redirectOf")).toString(), id);
        QVERIFY(account.smtp.delivered.isEmpty());
        // Back online, the Outbox goes out.
        MailServiceTestAccess::setOnline(service, true);
        MailServiceTestAccess::flushOutbox(service);
        QTRY_VERIFY2_WITH_TIMEOUT(account.smtp.delivered.size() == 1,
                                  qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(service->store()->drafts()).toJson(QJsonDocument::Compact))),
                                  15000);
        QVERIFY(account.smtp.delivered.first().contains("Resent-To: <carol@example.org>"));
        QVERIFY(account.smtp.delivered.first().contains("Subject: Plans"));
        QTRY_VERIFY(service->store()->drafts().isEmpty());
    }

    void orderExceptionsAndStopOnRunNow()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QString invoice = importLocal(env, QStringLiteral("1.eml"), mail("Grace <grace@navy.example>", "Invoice 12", "i12@x"));
        const QString boss = importLocal(env, QStringLiteral("2.eml"), mail("Boss <boss@navy.example>", "Invoice 13", "i13@x"));
        const QString other = importLocal(env, QStringLiteral("3.eml"), mail("Linus <linus@kernel.example>", "Hello", "h@x"));
        MailOrganizer *organizer = service->organizer();
        QVERIFY(organizer->saveRule(rule(QStringLiteral("Invoices"), {condition(QStringLiteral("subject"), {QStringLiteral("invoice")})},
            {action(QStringLiteral("setImportance"), QStringLiteral("high")), action(QStringLiteral("moveTo"), {}, QStringLiteral("acct:local:Archive"))},
            {condition(QStringLiteral("from"), {QStringLiteral("boss@")})})).value(QStringLiteral("ok")).toBool());
        QVERIFY(organizer->saveRule(rule(QStringLiteral("Grace"), {condition(QStringLiteral("from"), {QStringLiteral("grace")})},
            {action(QStringLiteral("assignCategory"), {}, {}, {QStringLiteral("Red category")}), action(QStringLiteral("stop"))})).value(QStringLiteral("ok")).toBool());
        QVERIFY(organizer->saveRule(rule(QStringLiteral("Everything"), {}, {action(QStringLiteral("flag"), QStringLiteral("today"))})).value(QStringLiteral("ok")).toBool());
        QSignalSpy ran(organizer, &MailOrganizer::rulesRunFinished);
        const QStringList all{organizer->rules().at(0).toMap().value(QStringLiteral("id")).toString(),
                              organizer->rules().at(1).toMap().value(QStringLiteral("id")).toString(),
                              organizer->rules().at(2).toMap().value(QStringLiteral("id")).toString()};
        const QString run = organizer->runRulesNow(all, QStringLiteral("acct:local:Inbox"), false, QStringLiteral("all"));
        const QVariantMap outcome = MailTest::waitResult(ran, run);
        QCOMPARE(outcome.value(QStringLiteral("processed")).toInt(), 3);
        QCOMPARE(outcome.value(QStringLiteral("matched")).toInt(), 3);
        const QVariantMap i12 = service->messageById(invoice);
        QCOMPARE(i12.value(QStringLiteral("folderKey")).toString(), QStringLiteral("acct:local:Archive"));
        QCOMPARE(i12.value(QStringLiteral("importance")).toString(), QStringLiteral("high"));
        QCOMPARE(i12.value(QStringLiteral("categories")).toStringList(), QStringList{QStringLiteral("Red category")});
        QVERIFY(!i12.value(QStringLiteral("flagged")).toBool());            // "stop" kept rule 3 away
        const QVariantMap i13 = service->messageById(boss);
        QCOMPARE(i13.value(QStringLiteral("folderKey")).toString(), QStringLiteral("acct:local:Inbox"));   // the exception
        QVERIFY(i13.value(QStringLiteral("flagged")).toBool());
        QCOMPARE(i13.value(QStringLiteral("followUpStatus")).toString(), QStringLiteral("flagged"));
        QVERIFY(service->messageById(other).value(QStringLiteral("flagged")).toBool());
        // Turned off, a rule does not run on arrival but Run Rules Now may choose it.
        QVERIFY(organizer->setRuleEnabled(all.at(2), false));
        QVERIFY(organizer->moveRule(all.at(2), 0));
        QCOMPARE(organizer->rules().first().toMap().value(QStringLiteral("id")).toString(), all.at(2));
    }

    void createRuleImportExport()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QString id = importLocal(env, QStringLiteral("r.eml"), mail("Grace <grace@navy.example>", "Re: Fwd: Quarterly plans", "q@x"));
        MailOrganizer *organizer = service->organizer();
        const QVariantMap made = organizer->ruleFromMessage(id);
        const QVariantMap made_rule = made.value(QStringLiteral("rule")).toMap();
        QCOMPARE(made_rule.value(QStringLiteral("name")).toString(), QStringLiteral("grace@navy.example"));
        QCOMPARE(made_rule.value(QStringLiteral("conditions")).toList().first().toMap().value(QStringLiteral("values")).toStringList(),
                 QStringList{QStringLiteral("grace@navy.example")});
        QCOMPARE(made.value(QStringLiteral("suggestions")).toMap().value(QStringLiteral("subject")).toStringList(),
                 QStringList{QStringLiteral("Quarterly plans")});
        // A rule needs an action, and a folder this computer has.
        QVERIFY(!organizer->validateRule(made_rule).isEmpty());
        QVariantMap bad = made_rule;
        bad.insert(QStringLiteral("actions"), QVariantList{action(QStringLiteral("moveTo"), {}, QStringLiteral("acct:nowhere:Stuff"))});
        QVERIFY(organizer->validateRule(bad).join(QLatin1Char(' ')).contains(QStringLiteral("no such folder")));
        QVariantMap good = made_rule;
        good.insert(QStringLiteral("actions"), QVariantList{action(QStringLiteral("moveTo"), {}, QStringLiteral("acct:local:Archive"))});
        QVERIFY(organizer->saveRule(good).value(QStringLiteral("ok")).toBool());
        QCOMPARE(organizer->rules().size(), 1);
        QVERIFY(!organizer->copyRule(organizer->rules().first().toMap().value(QStringLiteral("id")).toString()).isEmpty());
        QCOMPARE(organizer->rules().size(), 2);
        // Export, delete, import: the rules come back; a foreign folder comes in off.
        const QString file = env.dir.filePath(QStringLiteral("rules.json"));
        QCOMPARE(organizer->exportRules(QUrl::fromLocalFile(file)).value(QStringLiteral("count")).toInt(), 2);
        QFile raw(file);
        QVERIFY(raw.open(QIODevice::ReadOnly));
        QByteArray json = raw.readAll();
        raw.close();
        QVERIFY(json.contains("qindaoffice.mail.rules/1"));
        json.replace("acct:local:Archive", "acct:elsewhere:Archive");
        QFile edited(env.dir.filePath(QStringLiteral("foreign.json")));
        QVERIFY(edited.open(QIODevice::WriteOnly));
        edited.write(json);
        edited.close();
        for (const QVariant &value : organizer->rules()) QVERIFY(organizer->deleteRule(value.toMap().value(QStringLiteral("id")).toString()));
        QVERIFY(organizer->rules().isEmpty());
        QCOMPARE(organizer->importRules(QUrl::fromLocalFile(file)).value(QStringLiteral("added")).toInt(), 2);
        const QVariantMap foreign = organizer->importRules(QUrl::fromLocalFile(edited.fileName()));
        QCOMPARE(foreign.value(QStringLiteral("added")).toInt(), 2);
        QCOMPARE(foreign.value(QStringLiteral("problems")).toStringList().size(), 2);
        QCOMPARE(organizer->rules().size(), 4);
        QVERIFY(!organizer->rules().last().toMap().value(QStringLiteral("enabled")).toBool());
        // The rules outlive the process.
        auto reopened = env.reopen();
        QCOMPARE(reopened->organizer()->rules().size(), 4);
    }

    void ignoreAndCleanUpConversation()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QByteArray first = mail("Grace <grace@navy.example>", "Plan", "p1@x", "Let us meet at noon on Friday.");
        const QByteArray reply = mail("Bob <bob@example.org>", "Re: Plan", "p2@x",
                                      "Fine by me.\r\n\r\nOn Thu, Grace wrote:\r\n> Let us meet at noon\r\n> on Friday.",
                                      "In-Reply-To: <p1@x>\r\nReferences: <p1@x>\r\n");
        const QString a = importLocal(env, QStringLiteral("p1.eml"), first);
        const QString b = importLocal(env, QStringLiteral("p2.eml"), reply);
        QSignalSpy done(service, &MailService::operationFinished);
        QCOMPARE(MailTest::waitState(done, service->markRead({a, b}, true)).first, QStringLiteral("accepted"));
        // Clean Up: the first is quoted whole by the reply.
        QCOMPARE(MailTest::waitState(done, service->organizer()->cleanUpConversation(b)).first, QStringLiteral("accepted"));
        QCOMPARE(service->messageById(a).value(QStringLiteral("folderKey")).toString(), QStringLiteral("acct:local:Trash"));
        QCOMPARE(service->messageById(b).value(QStringLiteral("folderKey")).toString(), QStringLiteral("acct:local:Inbox"));
        // Ignore: the rest goes too, and asking again stops ignoring.
        QVERIFY(!service->organizer()->isIgnored(b));
        QCOMPARE(MailTest::waitState(done, service->organizer()->ignoreConversation(b)).first, QStringLiteral("accepted"));
        QVERIFY(service->organizer()->isIgnored(b));
        QCOMPARE(service->messageById(b).value(QStringLiteral("folderKey")).toString(), QStringLiteral("acct:local:Trash"));
        static_cast<void>(service->organizer()->ignoreConversation(b));
        QVERIFY(!service->organizer()->isIgnored(b));
    }

    void ignoredConversationArrivalsGoToTrash()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        ImapAccount account;
        account.imap.inbox.append({1, mail("Grace <grace@navy.example>", "Thread", "t1@x"), false});
        QVERIFY(account.add(service));
        QCOMPARE(sync(service, account.key(QStringLiteral("INBOX"))), QStringLiteral("accepted"));
        QSignalSpy done(service, &MailService::operationFinished);
        QCOMPARE(MailTest::waitState(done, service->organizer()->ignoreConversation(account.record(1))).first, QStringLiteral("accepted"));
        QTRY_VERIFY(account.imap.folders.value(QStringLiteral("Trash")).size() == 1);
        account.imap.inbox.append({2, mail("Bob <bob@example.org>", "Re: Thread", "t2@x", "More", "In-Reply-To: <t1@x>\r\nReferences: <t1@x>\r\n"), false});
        QCOMPARE(sync(service, account.key(QStringLiteral("INBOX"))), QStringLiteral("accepted"));
        QTRY_VERIFY_WITH_TIMEOUT(account.imap.folders.value(QStringLiteral("Trash")).size() == 2, 10000);
        QVERIFY(!hasUid(account.imap.inbox, 2));
    }
};

MAIL_TEST_MAIN(TestMailRules)

#include "tst_qindamail_rules.moc"
