// SPDX-License-Identifier: GPL-3.0-or-later
// M81 (D-090): categories, follow-up flags, search folders and automatic
// replies through QindaMail's service — the master lists (Outlook's six,
// add, rename everywhere, delete), categories on mail (IMAP keywords both
// ways, a server without keywords keeping them here), events, tasks and
// contacts; flags as \Flagged with Outlook's dates, on the task board and
// moved there; an Exchange account's tags and automatic replies through
// the EWS fake; live search folders; automatic replies over a fake
// ManageSieve server and plainly unavailable for POP3. Local servers only.

#include "mail_test_support.h"

#include "fake_imap_server.h"
#include "fake_mail_server.h"
#include "office_mail/fake_managesieve_server.h"
#include "ews_fake_server.h"

#include "qindamail/src/account_runtime.h"
#include "qindamail/src/mail_organizer.h"

#include "contact_store.h"
#include "sieve_script.h"

#include <qindatk/local_productivity_store.h>

#include <QFile>

using namespace QindaOffice::Apps::Mail;

namespace {

QByteArray mail(const QByteArray &from, const QByteArray &subject, const QByteArray &id, const QByteArray &extra = {})
{
    return "From: " + from + "\r\nTo: Ada <ada@work.example>\r\nSubject: " + subject
        + "\r\nDate: Thu, 8 Oct 2026 09:00:00 +0000\r\nMessage-ID: <" + id + ">\r\n" + extra
        + "MIME-Version: 1.0\r\nContent-Type: text/plain\r\n\r\nBody of " + subject + "\r\n";
}

QVariantMap server(quint16 port, bool incoming, const QString &protocol = QStringLiteral("imap"))
{
    QVariantMap map{{QStringLiteral("host"), QStringLiteral("127.0.0.1")}, {QStringLiteral("port"), port},
                    {QStringLiteral("security"), QStringLiteral("none")}, {QStringLiteral("userName"), QStringLiteral("ada")}};
    if (incoming) map.insert(QStringLiteral("protocol"), protocol);
    return map;
}

QString addImap(MailService *service, const MailTest::FakeImapServer &imap, const QVariantMap &extra = {})
{
    QVariantMap account{{QStringLiteral("kind"), QStringLiteral("imap")}, {QStringLiteral("displayName"), QStringLiteral("Work")},
                        {QStringLiteral("emailAddress"), QStringLiteral("ada@work.example")},
                        {QStringLiteral("incoming"), server(imap.port(), true)}, {QStringLiteral("outgoing"), server(imap.port(), false)}};
    for (auto it = extra.cbegin(); it != extra.cend(); ++it) account.insert(it.key(), it.value());
    return service->addAccount(account, QStringLiteral("pw")).value(QStringLiteral("id")).toString();
}

QString syncInbox(MailService *service, const QString &account)
{
    QSignalSpy finished(service, &MailService::operationFinished);
    return MailTest::waitState(finished, service->syncFolder(QStringLiteral("acct:") + account + QStringLiteral(":INBOX"))).first;
}

QString tags(MailOrganizer *organizer, const QString &requestId)
{
    QSignalSpy spy(organizer, &MailOrganizer::tagsFinished);
    return MailTest::waitResult(spy, requestId).value(QStringLiteral("state")).toString();
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

class TestMailOrganize : public QObject {
    Q_OBJECT
private slots:
    void masterListsAndRename()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailOrganizer *organizer = service->organizer();
        QCOMPARE(organizer->categoriesFor(QStringLiteral("local")).size(), 6);                 // Outlook's six
        QCOMPARE(organizer->categoryColor(QStringLiteral("local"), QStringLiteral("blue category")).isEmpty(), false);
        QVERIFY(organizer->addCategory(QStringLiteral("local"), QStringLiteral("Projects"), QStringLiteral("#12ab34")).value(QStringLiteral("ok")).toBool());
        QVERIFY(!organizer->addCategory(QStringLiteral("local"), QStringLiteral("projects"), QString()).value(QStringLiteral("ok")).toBool());
        // One message, event, task and contact with the category.
        const QString message = importLocal(env, QStringLiteral("m.eml"), mail("Grace <grace@x.example>", "Plans", "plans@x"));
        QSignalSpy events(service, &MailService::eventChangeFinished);
        service->createEvent({{QStringLiteral("id"), QStringLiteral("event:1")}, {QStringLiteral("title"), QStringLiteral("Review")},
                              {QStringLiteral("startLocal"), QStringLiteral("2026-10-12T09:00:00")}, {QStringLiteral("endLocal"), QStringLiteral("2026-10-12T10:00:00")}});
        const QString task = service->addTask(QStringLiteral("Draft the plan"));
        const QVariantMap contact = service->contacts()->add({{QStringLiteral("displayName"), QStringLiteral("Grace Hopper")}});
        const QString contactId = contact.value(QStringLiteral("id")).toString();
        for (const auto &[kind, id] : {std::pair{QStringLiteral("message"), message}, std::pair{QStringLiteral("event"), QStringLiteral("event:1")},
                                       std::pair{QStringLiteral("task"), task}, std::pair{QStringLiteral("contact"), contactId}}) {
            QCOMPARE(tags(organizer, organizer->toggleCategory(kind, {id}, QStringLiteral("Projects"), true)), QStringLiteral("accepted"));
            QCOMPARE(organizer->categoriesOf(kind, id), QStringList{QStringLiteral("Projects")});
        }
        // Renamed in the list: renamed on every item.
        QVERIFY(organizer->updateCategory(QStringLiteral("local"), QStringLiteral("Projects"), {{QStringLiteral("name"), QStringLiteral("Plans")}}).value(QStringLiteral("ok")).toBool());
        for (const auto &[kind, id] : {std::pair{QStringLiteral("message"), message}, std::pair{QStringLiteral("event"), QStringLiteral("event:1")},
                                       std::pair{QStringLiteral("task"), task}, std::pair{QStringLiteral("contact"), contactId}})
            QCOMPARE(organizer->categoriesOf(kind, id), QStringList{QStringLiteral("Plans")});
        QVERIFY(organizer->updateCategory(QStringLiteral("local"), QStringLiteral("Plans"), {{QStringLiteral("color"), QStringLiteral("#0078d4")}}).value(QStringLiteral("ok")).toBool());
        QCOMPARE(organizer->categoryColor(QStringLiteral("local"), QStringLiteral("Plans")), QStringLiteral("#0078d4"));
        // Off the list: the items keep the name, with no colour.
        QVERIFY(organizer->deleteCategory(QStringLiteral("local"), QStringLiteral("Plans")));
        QCOMPARE(organizer->categoriesOf(QStringLiteral("message"), message), QStringList{QStringLiteral("Plans")});
        const QVariantList chips = service->messageById(message).value(QStringLiteral("categoryChips")).toList();
        QCOMPARE(chips.first().toMap().value(QStringLiteral("color")).toString(), QString());
        // Search and filter by category.
        service->setCurrentFolderKey(QStringLiteral("acct:local:Inbox"));
        service->setSearchText(QStringLiteral("category:plans"));
        QTRY_COMPARE(service->messageModel()->rowCount(), 1);
        service->setSearchText(QStringLiteral("category:travel"));
        QCOMPARE(service->messageModel()->rowCount(), 0);
        service->setSearchText(QString());
        service->messageModel()->setCategoryFilter(QStringLiteral("*"));
        QCOMPARE(service->messageModel()->rowCount(), 1);
        QVERIFY(!service->contacts()->search(QStringLiteral("Plans")).isEmpty());           // People finds it too
        auto reopened = env.reopen();
        QCOMPARE(reopened->organizer()->categoriesFor(QStringLiteral("local")).size(), 6);  // 6 + Projects − Plans
    }

    void categoriesAsImapKeywords()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeImapServer imap("pw");
        imap.inbox.append({1, mail("Grace <grace@x.example>", "One", "one@x"), false});
        imap.inbox.append({2, mail("Bob <bob@x.example>", "Two", "two@x"), false, false, {QStringLiteral("Red_category"), QStringLiteral("$Forwarded")}});
        const QString account = addImap(service, imap);
        QCOMPARE(syncInbox(service, account), QStringLiteral("accepted"));
        const QString one = QStringLiteral("imap:%1:INBOX:4242:1").arg(account);
        const QString two = QStringLiteral("imap:%1:INBOX:4242:2").arg(account);
        // The server's keyword came in as a category ($Forwarded is not one).
        QTRY_COMPARE(service->organizer()->categoriesOf(QStringLiteral("message"), two), QStringList{QStringLiteral("Red category")});
        MailOrganizer *organizer = service->organizer();
        QCOMPARE(tags(organizer, organizer->setCategories(QStringLiteral("message"), {one}, {QStringLiteral("Blue category"), QStringLiteral("R&D")})),
                 QStringLiteral("accepted"));
        QCOMPARE(imap.inbox.first().keywords, (QStringList{QStringLiteral("Blue_category"), QStringLiteral("R&-D")}));
        QCOMPARE(tags(organizer, organizer->toggleCategory(QStringLiteral("message"), {one}, QStringLiteral("Blue category"), false)),
                 QStringLiteral("accepted"));
        QCOMPARE(imap.inbox.first().keywords, QStringList{QStringLiteral("R&-D")});
        QVERIFY(!service->messageById(one).value(QStringLiteral("tagsPending")).toBool());
        // Another client tags message one: the next sync brings it.
        imap.inbox[0].keywords.append(QStringLiteral("Travel"));
        QCOMPARE(syncInbox(service, account), QStringLiteral("accepted"));
        QTRY_COMPARE(organizer->categoriesOf(QStringLiteral("message"), one), (QStringList{QStringLiteral("R&D"), QStringLiteral("Travel")}));

        // A server that keeps no keywords: categories stay here, unharmed by syncs.
        MailTest::FakeImapServer plain("pw");
        plain.keywords = false;
        plain.inbox.append({1, mail("Grace <grace@x.example>", "Three", "three@x"), false});
        const QString second = addImap(service, plain);
        QCOMPARE(syncInbox(service, second), QStringLiteral("accepted"));
        const QString three = QStringLiteral("imap:%1:INBOX:4242:1").arg(second);
        QSignalSpy notices(service, &MailService::notify);
        QCOMPARE(tags(organizer, organizer->setCategories(QStringLiteral("message"), {three}, {QStringLiteral("Green category")})), QStringLiteral("accepted"));
        QTRY_VERIFY(!notices.isEmpty());
        QCOMPARE(notices.first().at(0).toString(), QStringLiteral("Categories stay on this computer"));
        QCOMPARE(syncInbox(service, second), QStringLiteral("accepted"));
        QTest::qWait(300);
        QCOMPARE(organizer->categoriesOf(QStringLiteral("message"), three), QStringList{QStringLiteral("Green category")});
        QVERIFY(plain.inbox.first().keywords.isEmpty());
    }

    void followUpFlagsAndTheTaskBoard()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeImapServer imap("pw");
        imap.inbox.append({1, mail("Grace <grace@x.example>", "Review the draft", "draft@x"), false});
        const QString account = addImap(service, imap);
        QCOMPARE(syncInbox(service, account), QStringLiteral("accepted"));
        const QString id = QStringLiteral("imap:%1:INBOX:4242:1").arg(account);
        MailOrganizer *organizer = service->organizer();
        organizer->setToday(QDate(2026, 10, 7));                                              // a Wednesday
        QCOMPARE(tags(organizer, organizer->followUp({id}, QStringLiteral("thisWeek"))), QStringLiteral("accepted"));
        QVERIFY(imap.inbox.first().flagged);
        QVariantMap flag = organizer->followUpOf(id);
        QCOMPARE(flag.value(QStringLiteral("status")).toString(), QStringLiteral("flagged"));
        QCOMPARE(flag.value(QStringLiteral("start")).toString(), QStringLiteral("2026-10-09"));
        QCOMPARE(flag.value(QStringLiteral("due")).toString(), QStringLiteral("2026-10-09"));
        QVERIFY(flag.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Follow up.")));
        QCOMPARE(service->messageById(id).value(QStringLiteral("followUpDue")).toString().isEmpty(), false);
        // Flagged mail is a card on the board (To do), Done marks it complete.
        QString cardId;
        for (const QVariant &column : service->taskColumns()) {
            for (const QVariant &card : column.toMap().value(QStringLiteral("tasks")).toList())
                if (card.toMap().value(QStringLiteral("isMail")).toBool()) {
                    cardId = card.toMap().value(QStringLiteral("taskId")).toString();
                    QCOMPARE(column.toMap().value(QStringLiteral("status")).toString(), QStringLiteral("todo"));
                    QCOMPARE(card.toMap().value(QStringLiteral("dueDate")).toString(), QStringLiteral("2026-10-09"));
                    QCOMPARE(card.toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Review the draft"));
                }
        }
        QCOMPARE(cardId, QStringLiteral("mailflag:") + id);
        QSignalSpy done(service, &MailService::operationFinished);
        service->moveTask(QStringLiteral("move-1"), cardId, QStringLiteral("done"), -1);
        QCOMPARE(MailTest::waitState(done, QStringLiteral("move-1")).first, QStringLiteral("accepted"));
        QCOMPARE(organizer->followUpOf(id).value(QStringLiteral("status")).toString(), QStringLiteral("complete"));
        QTRY_VERIFY(!imap.inbox.first().flagged);                                           // IMAP knows no "complete"
        // Custom dates, then Clear Flag.
        QCOMPARE(tags(organizer, organizer->followUpCustom({id}, QStringLiteral("2026-10-20"), QStringLiteral("2026-10-23"))), QStringLiteral("accepted"));
        QCOMPARE(organizer->followUpOf(id).value(QStringLiteral("due")).toString(), QStringLiteral("2026-10-23"));
        QVERIFY(imap.inbox.first().flagged);
        QCOMPARE(tags(organizer, organizer->followUp({id}, QStringLiteral("clear"))), QStringLiteral("accepted"));
        QVERIFY(!imap.inbox.first().flagged);
        QVERIFY(organizer->followUpOf(id).value(QStringLiteral("status")).toString().isEmpty());
        for (const QVariant &column : service->taskColumns())
            QVERIFY(column.toMap().value(QStringLiteral("tasks")).toList().isEmpty());
    }

    void exchangeTagsAndRepliesThroughEws()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeImapServer imap("pw");
        imap.inbox.append({1, mail("Grace <grace@x.example>", "Budget", "budget@x"), false});
        imap.inbox.append({2, mail("Bob <bob@x.example>", "Lunch", "lunch@x"), false});
        EwsFakeServer ews;
        ews.http.authorization = "Basic " + QByteArray("ada:pw").toBase64();
        EwsFakeServer::MailItem budget;
        budget.internetMessageId = QStringLiteral("<budget@x>");
        budget.received = QDateTime::currentDateTimeUtc().addDays(-1);
        ews.mail.insert(QStringLiteral("MSG1"), budget);
        EwsFakeServer::MailItem lunch = budget;
        lunch.internetMessageId = QStringLiteral("<lunch@x>");
        lunch.categories = {QStringLiteral("Purple category")};
        lunch.flagStatus = QStringLiteral("Flagged");
        lunch.due = QDateTime(QDate(2026, 10, 15), QTime(12, 0), QTimeZone::UTC);   // the same day in any zone
        ews.mail.insert(QStringLiteral("MSG2"), lunch);
        const QString account = service->addAccount(
            {{QStringLiteral("kind"), QStringLiteral("exchange")}, {QStringLiteral("displayName"), QStringLiteral("Office")},
             {QStringLiteral("emailAddress"), QStringLiteral("ada@work.example")},
             {QStringLiteral("incoming"), server(imap.port(), true)},
             {QStringLiteral("calendar"), QVariantMap{{QStringLiteral("protocol"), QStringLiteral("ews")}, {QStringLiteral("url"), ews.url()},
                                                      {QStringLiteral("userName"), QStringLiteral("ada")}, {QStringLiteral("authMechanism"), QStringLiteral("basic")}}}},
            QStringLiteral("pw")).value(QStringLiteral("id")).toString();
        QVERIFY(!account.isEmpty());
        QCOMPARE(service->runtime(account)->mailboxService(), QStringLiteral("ews"));
        QCOMPARE(syncInbox(service, account), QStringLiteral("accepted"));
        const QString one = QStringLiteral("imap:%1:INBOX:4242:1").arg(account);
        const QString two = QStringLiteral("imap:%1:INBOX:4242:2").arg(account);
        MailOrganizer *organizer = service->organizer();
        // The server's tags came back after the sync …
        QTRY_COMPARE(organizer->categoriesOf(QStringLiteral("message"), two), QStringList{QStringLiteral("Purple category")});
        QCOMPARE(organizer->followUpOf(two).value(QStringLiteral("due")).toString(), QStringLiteral("2026-10-15"));
        // … and ours go there (no IMAP keywords for an Exchange mailbox).
        QCOMPARE(tags(organizer, organizer->setCategories(QStringLiteral("message"), {one}, {QStringLiteral("Red category")})), QStringLiteral("accepted"));
        QCOMPARE(ews.mail.value(QStringLiteral("MSG1")).categories, QStringList{QStringLiteral("Red category")});
        QVERIFY(imap.inbox.first().keywords.isEmpty());
        organizer->setToday(QDate(2026, 10, 7));
        QCOMPARE(tags(organizer, organizer->followUp({one}, QStringLiteral("tomorrow"))), QStringLiteral("accepted"));
        QCOMPARE(ews.mail.value(QStringLiteral("MSG1")).flagStatus, QStringLiteral("Flagged"));
        QVERIFY(ews.mail.value(QStringLiteral("MSG1")).due.isValid());
        QVERIFY(imap.inbox.first().flagged);                                                // \Flagged too
        // Automatic replies are Exchange's OOF.
        QVariantList ways = organizer->autoReplyAccounts();
        QCOMPARE(ways.first().toMap().value(QStringLiteral("method")).toString(), QStringLiteral("ews"));
        QSignalSpy replies(organizer, &MailOrganizer::autoRepliesFinished);
        QVariantMap result = MailTest::waitResult(replies, organizer->writeAutoReplies(account,
            {{QStringLiteral("enabled"), true}, {QStringLiteral("internalMessage"), QStringLiteral("Away")}, {QStringLiteral("externalAudience"), QStringLiteral("none")}}));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(ews.oofState, QStringLiteral("Enabled"));
        QCOMPARE(ews.oofAudience, QStringLiteral("None"));
        result = MailTest::waitResult(replies, organizer->readAutoReplies(account));
        QVERIFY(result.value(QStringLiteral("settings")).toMap().value(QStringLiteral("enabled")).toBool());
        QCOMPARE(result.value(QStringLiteral("settings")).toMap().value(QStringLiteral("internalMessage")).toString(), QStringLiteral("Away"));
        QCOMPARE(service->accountById(account).value(QStringLiteral("status")).toString() == QLatin1String("error"), false);
    }

    void searchFoldersStayLive()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        const QString a = importLocal(env, QStringLiteral("a.eml"), mail("Grace <grace@x.example>", "A", "a@x"));
        const QString b = importLocal(env, QStringLiteral("b.eml"), mail("Bob <bob@x.example>", "B", "b@x"));
        QSignalSpy unread(service, &MailService::operationFinished);
        QCOMPARE(MailTest::waitState(unread, service->markRead({a, b}, false)).first, QStringLiteral("accepted"));   // opened files come in read
        MailOrganizer *organizer = service->organizer();
        QVariantMap folder = organizer->newSearchFolder(QStringLiteral("unread"));
        QCOMPARE(folder.value(QStringLiteral("name")).toString(), QStringLiteral("Unread Mail"));
        const QVariantMap saved = organizer->saveSearchFolder(folder);
        QVERIFY(saved.value(QStringLiteral("ok")).toBool());
        const QString key = saved.value(QStringLiteral("key")).toString();
        QTRY_VERIFY(service->folderModel()->indexOfKey(key) >= 0);
        QVERIFY(service->folderModel()->indexOfKey(QStringLiteral("searches")) >= 0);
        QTRY_COMPARE(service->folderModel()->get(service->folderModel()->indexOfKey(key)).value(QStringLiteral("unread")).toInt(), 2);
        service->setCurrentFolderKey(key);
        QCOMPARE(service->currentFolderName(), QStringLiteral("Unread Mail"));
        QCOMPARE(service->messageModel()->rowCount(), 2);
        // Read one: it leaves the folder; deleted (Trash) ones never show.
        QSignalSpy done(service, &MailService::operationFinished);
        QCOMPARE(MailTest::waitState(done, service->markRead({a}, true)).first, QStringLiteral("accepted"));
        QTRY_COMPARE(service->messageModel()->rowCount(), 1);
        QCOMPARE(MailTest::waitState(done, service->deleteMessages({b})).first, QStringLiteral("accepted"));
        QTRY_COMPARE(service->messageModel()->rowCount(), 0);
        QTRY_COMPARE(service->folderModel()->get(service->folderModel()->indexOfKey(key)).value(QStringLiteral("total")).toInt(), 0);
        // A custom folder (from Grace, read), then deleting folders.
        QVariantMap custom = organizer->newSearchFolder(QStringLiteral("custom"));
        custom.insert(QStringLiteral("name"), QStringLiteral("Grace, read"));
        custom.insert(QStringLiteral("conditions"), QVariantList{QVariantMap{{QStringLiteral("type"), QStringLiteral("from")},
                                                                             {QStringLiteral("values"), QStringList{QStringLiteral("grace")}}}});
        const QString customKey = organizer->saveSearchFolder(custom).value(QStringLiteral("key")).toString();
        QCOMPARE(organizer->searchRecords(customKey).size(), 1);
        QVERIFY(organizer->deleteSearchFolder(key));
        QTRY_COMPARE(service->currentFolderKey(), QStringLiteral("acct:local:Inbox"));    // falls back
        QCOMPARE(organizer->searchFolders().size(), 1);
        auto reopened = env.reopen();
        QCOMPARE(reopened->organizer()->searchFolders().size(), 1);
    }

    void automaticRepliesOverSieveAndNowhere()
    {
        MailTest::Env env;
        MailService *service = env.service.get();
        MailTest::FakeImapServer imap("pw");
        MailTest::FakeManageSieveServer sieve("pw");
        sieve.scripts.insert(QStringLiteral("main"), "require \"fileinto\";\r\nif header :contains \"subject\" \"x\" { fileinto \"X\"; }\r\n");
        sieve.active = QStringLiteral("main");
        const QString account = addImap(service, imap, {{QStringLiteral("sieve"), QVariantMap{{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
                                                                                             {QStringLiteral("port"), sieve.port()},
                                                                                             {QStringLiteral("security"), QStringLiteral("none")}}}});
        MailTest::FakeMailServer pop(MailTest::FakeMailServer::Kind::Pop3, "pw");
        const QString popAccount = service->addAccount({{QStringLiteral("kind"), QStringLiteral("pop3")}, {QStringLiteral("displayName"), QStringLiteral("Home")},
                                                        {QStringLiteral("emailAddress"), QStringLiteral("ada@home.example")},
                                                        {QStringLiteral("incoming"), server(pop.port(), true, QStringLiteral("pop3"))},
                                                        {QStringLiteral("outgoing"), server(pop.port(), false)}},
                                                       QStringLiteral("pw")).value(QStringLiteral("id")).toString();
        MailOrganizer *organizer = service->organizer();
        QSignalSpy replies(organizer, &MailOrganizer::autoRepliesFinished);
        QVariantMap result = MailTest::waitResult(replies, organizer->readAutoReplies(account));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(result.value(QStringLiteral("method")).toString(), QStringLiteral("sieve"));
        QVERIFY(!result.value(QStringLiteral("settings")).toMap().value(QStringLiteral("enabled")).toBool());
        result = MailTest::waitResult(replies, organizer->writeAutoReplies(account,
            {{QStringLiteral("enabled"), true}, {QStringLiteral("internalMessage"), QStringLiteral("I am away.")}, {QStringLiteral("subject"), QStringLiteral("Away")}}));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        const QString script = QString::fromUtf8(sieve.scripts.value(QStringLiteral("main")));
        QVERIFY(QindaOffice::Mail::Sieve::hasManagedVacation(script));
        QVERIFY(script.contains(QStringLiteral(":addresses [\"ada@work.example\"]")));
        QVERIFY(script.contains(QStringLiteral("fileinto \"X\"")));
        // Off again: the server's script as it was; the text is remembered here.
        result = MailTest::waitResult(replies, organizer->writeAutoReplies(account, {{QStringLiteral("enabled"), false},
                                                                                       {QStringLiteral("internalMessage"), QStringLiteral("I am away.")}}));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(!QindaOffice::Mail::Sieve::hasManagedVacation(QString::fromUtf8(sieve.scripts.value(QStringLiteral("main")))));
        result = MailTest::waitResult(replies, organizer->readAutoReplies(account));
        QCOMPARE(result.value(QStringLiteral("settings")).toMap().value(QStringLiteral("internalMessage")).toString(), QStringLiteral("I am away."));
        // POP3: said plainly; the account itself stays fine.
        result = MailTest::waitResult(replies, organizer->readAutoReplies(popAccount));
        QVERIFY(!result.value(QStringLiteral("available")).toBool());
        QVERIFY(result.value(QStringLiteral("error")).toString().contains(QStringLiteral("POP3")));
        // No ManageSieve on the server: not available, and the account's status is untouched.
        sieve.extensions = {QStringLiteral("fileinto")};
        result = MailTest::waitResult(replies, organizer->readAutoReplies(account));
        QVERIFY(!result.value(QStringLiteral("available")).toBool());
        QCOMPARE(service->accountById(account).value(QStringLiteral("status")).toString(), QStringLiteral("idle"));
    }
};

MAIL_TEST_MAIN(TestMailOrganize)

#include "tst_qindamail_organize.moc"
