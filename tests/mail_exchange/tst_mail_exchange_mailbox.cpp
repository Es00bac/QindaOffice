// SPDX-License-Identifier: GPL-3.0-or-later
// ExchangeMailbox (M81, D-090) against the in-process Graph and EWS fakes on
// 127.0.0.1: automatic replies read and written (scheduled, audiences, text
// through the servers' HTML), the master category list (Graph adds, EWS
// reads Outlook's CategoryList), the server's inbox rules worded for the
// read-only list, a message's categories and follow-up flag found by its
// Internet message id (EWS also in a folder found by name), an event's
// categories (EWS without notifying attendees) read back by the calendar
// sync, pulling the tags of recent mail, refusals and the secret audit.
#include "ews_fixture.h"
#include "exchange_mailbox.h"
#include "graph_fixture.h"

#include <QJsonArray>

using QindaOffice::Mail::ExchangeMailbox;

namespace {

QVariantMap call(ExchangeMailbox &box, const char *signal, const std::function<void()> &start)
{
    QSignalSpy spy(&box, signal);
    start();
    return ExchangeTest::waitFor(spy);
}

QVariantMap replies(bool scheduled)
{
    return {{QStringLiteral("enabled"), true}, {QStringLiteral("scheduled"), scheduled},
            {QStringLiteral("start"), QStringLiteral("2026-10-12T08:00:00Z")}, {QStringLiteral("end"), QStringLiteral("2026-10-23T17:00:00Z")},
            {QStringLiteral("internalMessage"), QStringLiteral("Away until the 23rd.\nAsk <Grace> & co.")},
            {QStringLiteral("externalMessage"), QStringLiteral("I am out of the office.")},
            {QStringLiteral("externalAudience"), QStringLiteral("contacts")}};
}

void graphBox(ExchangeMailbox &box, const GraphFakeServer &server)
{
    box.setProperty("service", QStringLiteral("graph"));
    box.setProperty("baseUrl", server.base());
    box.setProperty("timeoutMs", 10000);
    box.setSecret(GraphTest::kToken, QStringLiteral("bearer"));
}

void ewsBox(ExchangeMailbox &box, const EwsFakeServer &server)
{
    box.setProperty("service", QStringLiteral("ews"));
    box.setProperty("url", server.url());
    box.setProperty("userName", EwsTest::kUser);
    box.setProperty("mailbox", EwsTest::kUser);
    box.setProperty("timeoutMs", 10000);
    box.setSecret(EwsTest::kPassword, QStringLiteral("basic"));
}

} // namespace

class MailboxTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { ExchangeTest::LogCapture::install(); }

    void graphAutoReplies()
    {
        GraphTest::Fixture f;
        ExchangeMailbox box;
        graphBox(box, f.server);
        QVariantMap result = call(box, SIGNAL(autoRepliesFinished(QString,QVariantMap)), [&] { box.readAutoReplies(QStringLiteral("r1")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        QVERIFY(!result.value(QStringLiteral("settings")).toMap().value(QStringLiteral("enabled")).toBool());
        result = call(box, SIGNAL(autoRepliesFinished(QString,QVariantMap)), [&] { box.writeAutoReplies(QStringLiteral("w1"), replies(true)); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        const QJsonObject stored = f.server.automaticReplies;
        QCOMPARE(stored.value(QLatin1String("status")).toString(), QStringLiteral("scheduled"));
        QCOMPARE(stored.value(QLatin1String("externalAudience")).toString(), QStringLiteral("contactsOnly"));
        QCOMPARE(stored.value(QLatin1String("scheduledStartDateTime")).toObject().value(QLatin1String("dateTime")).toString(),
                 QStringLiteral("2026-10-12T08:00:00"));
        QVERIFY(stored.value(QLatin1String("internalReplyMessage")).toString().contains(QStringLiteral("Ask &lt;Grace&gt; &amp; co.")));
        QVERIFY(stored.value(QLatin1String("internalReplyMessage")).toString().contains(QStringLiteral("<br>")));
        result = call(box, SIGNAL(autoRepliesFinished(QString,QVariantMap)), [&] { box.readAutoReplies(QStringLiteral("r2")); });
        const QVariantMap settings = result.value(QStringLiteral("settings")).toMap();
        QVERIFY(settings.value(QStringLiteral("enabled")).toBool() && settings.value(QStringLiteral("scheduled")).toBool());
        QCOMPARE(settings.value(QStringLiteral("start")).toString(), QStringLiteral("2026-10-12T08:00:00Z"));
        QCOMPARE(settings.value(QStringLiteral("internalMessage")).toString(), replies(true).value(QStringLiteral("internalMessage")).toString());
        QCOMPARE(settings.value(QStringLiteral("externalAudience")).toString(), QStringLiteral("contacts"));
        QVariantMap off = replies(false);
        off.insert(QStringLiteral("enabled"), false);
        result = call(box, SIGNAL(autoRepliesFinished(QString,QVariantMap)), [&] { box.writeAutoReplies(QStringLiteral("w2"), off); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.automaticReplies.value(QLatin1String("status")).toString(), QStringLiteral("disabled"));
    }

    void graphCategoriesAndRules()
    {
        GraphTest::Fixture f;
        f.server.masterCategories = {QJsonObject{{QStringLiteral("id"), QStringLiteral("c1")}, {QStringLiteral("displayName"), QStringLiteral("Red category")},
                                                 {QStringLiteral("color"), QStringLiteral("preset0")}},
                                     QJsonObject{{QStringLiteral("id"), QStringLiteral("c2")}, {QStringLiteral("displayName"), QStringLiteral("Plain")},
                                                 {QStringLiteral("color"), QStringLiteral("none")}}};
        ExchangeMailbox box;
        graphBox(box, f.server);
        QVariantMap result = call(box, SIGNAL(categoriesFinished(QString,QVariantMap)), [&] { box.listCategories(QStringLiteral("c")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        QVariantList list = result.value(QStringLiteral("categories")).toList();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value(QStringLiteral("preset")).toInt(), 0);
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("preset")).toInt(), -1);
        result = call(box, SIGNAL(categoriesFinished(QString,QVariantMap)), [&] { box.addCategory(QStringLiteral("a"), QStringLiteral("Travel"), 5); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("categories")).toList().size(), 3);
        QCOMPARE(f.server.masterCategories.last().value(QLatin1String("color")).toString(), QStringLiteral("preset5"));
        // Already there (any case): Graph's 409 is fine, nothing doubles.
        result = call(box, SIGNAL(categoriesFinished(QString,QVariantMap)), [&] { box.addCategory(QStringLiteral("b"), QStringLiteral("red CATEGORY"), 0); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.masterCategories.size(), 3);

        f.server.messageRules = {QJsonObject{
            {QStringLiteral("displayName"), QStringLiteral("Boss")}, {QStringLiteral("sequence"), 2}, {QStringLiteral("isEnabled"), false},
            {QStringLiteral("conditions"), QJsonObject{
                {QStringLiteral("fromAddresses"), QJsonArray{QJsonObject{{QStringLiteral("emailAddress"), QJsonObject{{QStringLiteral("address"), QStringLiteral("boss@example.com")}}}}}},
                {QStringLiteral("subjectContains"), QJsonArray{QStringLiteral("urgent")}}}},
            {QStringLiteral("actions"), QJsonObject{{QStringLiteral("moveToFolder"), QStringLiteral("AAMkFolder")}, {QStringLiteral("markAsRead"), true},
                                                    {QStringLiteral("stopProcessingRules"), true}}}}};
        result = call(box, SIGNAL(serverRulesFinished(QString,QVariantMap)), [&] { box.listServerRules(QStringLiteral("rules")); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        const QVariantMap rule = result.value(QStringLiteral("rules")).toList().value(0).toMap();
        QCOMPARE(rule.value(QStringLiteral("name")).toString(), QStringLiteral("Boss"));
        QVERIFY(!rule.value(QStringLiteral("enabled")).toBool());
        QCOMPARE(rule.value(QStringLiteral("summary")).toString(),
                 QStringLiteral("from boss@example.com and subject contains urgent → move to a folder, mark as read, stop processing more rules"));
    }

    void graphItemTags()
    {
        GraphTest::Fixture f;
        f.server.messages.insert(QStringLiteral("msg-1"), {{QStringLiteral("internetMessageId"), QStringLiteral("<o'neil.1@example.org>")},
                                                           {QStringLiteral("receivedDateTime"), QDateTime::currentDateTimeUtc().addDays(-2).toString(Qt::ISODate)},
                                                           {QStringLiteral("categories"), QJsonArray()},
                                                           {QStringLiteral("flag"), QJsonObject{{QStringLiteral("flagStatus"), QStringLiteral("notFlagged")}}}});
        ExchangeMailbox box;
        graphBox(box, f.server);
        const QVariantMap item{{QStringLiteral("kind"), QStringLiteral("message")}, {QStringLiteral("internetMessageId"), QStringLiteral("o'neil.1@example.org")},
                               {QStringLiteral("categories"), QStringList{QStringLiteral("Red category"), QStringLiteral("Travel")}},
                               {QStringLiteral("flag"), QVariantMap{{QStringLiteral("status"), QStringLiteral("flagged")}, {QStringLiteral("start"), QStringLiteral("2026-10-12")},
                                                                     {QStringLiteral("due"), QStringLiteral("2026-10-16")}}},
                               {QStringLiteral("zone"), QStringLiteral("America/Denver")}};
        QVariantMap result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] { box.setItemState(QStringLiteral("s1"), item); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        QVERIFY(result.value(QStringLiteral("found")).toBool());
        const QJsonObject message = f.server.messages.value(QStringLiteral("msg-1"));
        QCOMPARE(message.value(QLatin1String("categories")).toArray().size(), 2);
        const QJsonObject flag = message.value(QLatin1String("flag")).toObject();
        QCOMPARE(flag.value(QLatin1String("flagStatus")).toString(), QStringLiteral("flagged"));
        QCOMPARE(flag.value(QLatin1String("dueDateTime")).toObject().value(QLatin1String("dateTime")).toString(), QStringLiteral("2026-10-16T00:00:00"));
        QCOMPARE(flag.value(QLatin1String("dueDateTime")).toObject().value(QLatin1String("timeZone")).toString(), QStringLiteral("Mountain Standard Time"));
        bool quoted = false;
        for (const FakeHttpServer::Request &request : f.server.http.requests)
            quoted = quoted || request.query.queryItemValue(QStringLiteral("$filter"), QUrl::FullyDecoded)
                                   == QStringLiteral("internetMessageId eq '<o''neil.1@example.org>'");
        QVERIFY(quoted);
        // Mark complete, then a message the mailbox does not have.
        QVariantMap done = item;
        done.remove(QStringLiteral("categories"));
        done.insert(QStringLiteral("flag"), QVariantMap{{QStringLiteral("status"), QStringLiteral("complete")}, {QStringLiteral("completed"), QStringLiteral("2026-10-14T10:00:00Z")}});
        result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] { box.setItemState(QStringLiteral("s2"), done); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.messages.value(QStringLiteral("msg-1")).value(QLatin1String("flag")).toObject().value(QLatin1String("flagStatus")).toString(),
                 QStringLiteral("complete"));
        QCOMPARE(f.server.messages.value(QStringLiteral("msg-1")).value(QLatin1String("categories")).toArray().size(), 2);   // unchanged
        QVariantMap missing = item;
        missing.insert(QStringLiteral("internetMessageId"), QStringLiteral("nobody@example.org"));
        result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] { box.setItemState(QStringLiteral("s3"), missing); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool() && !result.value(QStringLiteral("found")).toBool());

        // An event's categories, then read back by the calendar sync.
        QVERIFY(ExchangeTest::syncNow(f.provider, QStringLiteral("sync-0")).value(QStringLiteral("ok")).toBool());
        QVERIFY(f.event(QStringLiteral("Design review")).value(QStringLiteral("categories")).toStringList().isEmpty());
        result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] {
            box.setItemState(QStringLiteral("s4"), {{QStringLiteral("kind"), QStringLiteral("event")}, {QStringLiteral("remoteId"), QStringLiteral("ev-review")},
                                                    {QStringLiteral("categories"), QStringList{QStringLiteral("Travel")}}});
        });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(ExchangeTest::syncNow(f.provider, QStringLiteral("sync-1")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.event(QStringLiteral("Design review")).value(QStringLiteral("categories")).toStringList(), QStringList{QStringLiteral("Travel")});

        // Recent mail's tags, every page.
        for (int i = 2; i <= 5; ++i)
            f.server.messages.insert(QStringLiteral("msg-%1").arg(i), {{QStringLiteral("internetMessageId"), QStringLiteral("<m%1@example.org>").arg(i)},
                {QStringLiteral("receivedDateTime"), QDateTime::currentDateTimeUtc().addDays(i == 5 ? -90 : -1).toString(Qt::ISODate)},
                {QStringLiteral("categories"), QJsonArray{QStringLiteral("Blue category")}},
                {QStringLiteral("flag"), QJsonObject{{QStringLiteral("flagStatus"), QStringLiteral("flagged")},
                                                     {QStringLiteral("dueDateTime"), QJsonObject{{QStringLiteral("dateTime"), QStringLiteral("2026-10-20T00:00:00.0000000")},
                                                                                                 {QStringLiteral("timeZone"), QStringLiteral("UTC")}}}}}});
        result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] { box.pullItemStates(QStringLiteral("p"), 30); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        const QVariantList items = result.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 4);                                         // msg-5 is older than 30 days
        QVariantMap m2;
        for (const QVariant &value : items)
            if (value.toMap().value(QStringLiteral("internetMessageId")).toString() == QStringLiteral("m2@example.org")) m2 = value.toMap();
        QCOMPARE(m2.value(QStringLiteral("categories")).toStringList(), QStringList{QStringLiteral("Blue category")});
        QCOMPARE(m2.value(QStringLiteral("flag")).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("flagged"));
        QCOMPARE(m2.value(QStringLiteral("flag")).toMap().value(QStringLiteral("due")).toString(), QStringLiteral("2026-10-20"));
    }

    void ewsAutoRepliesCategoriesRules()
    {
        EwsTest::Fixture f;
        ExchangeMailbox box;
        ewsBox(box, f.server);
        f.server.oofState = QStringLiteral("Enabled");
        f.server.oofInternal = QStringLiteral("<html><body><p>Back <b>Monday</b>.</p><p>Thanks &amp; bye</p></body></html>");
        QVariantMap result = call(box, SIGNAL(autoRepliesFinished(QString,QVariantMap)), [&] { box.readAutoReplies(QStringLiteral("r")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        QVariantMap settings = result.value(QStringLiteral("settings")).toMap();
        QVERIFY(settings.value(QStringLiteral("enabled")).toBool() && !settings.value(QStringLiteral("scheduled")).toBool());
        QCOMPARE(settings.value(QStringLiteral("internalMessage")).toString(), QStringLiteral("Back Monday.\nThanks & bye"));
        result = call(box, SIGNAL(autoRepliesFinished(QString,QVariantMap)), [&] { box.writeAutoReplies(QStringLiteral("w"), replies(true)); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        QCOMPARE(f.server.oofState, QStringLiteral("Scheduled"));
        QCOMPARE(f.server.oofAudience, QStringLiteral("Known"));
        QCOMPARE(f.server.oofStart, QDateTime(QDate(2026, 10, 12), QTime(8, 0), QTimeZone::UTC));
        QVERIFY(f.server.oofExternal.contains(QStringLiteral("I am out of the office.")));
        QVERIFY(f.server.lastOperation.value(QStringLiteral("SetUserOofSettingsRequest")).find(u"Address") != nullptr);

        // Outlook's CategoryList (never customised → none), then a stored one.
        result = call(box, SIGNAL(categoriesFinished(QString,QVariantMap)), [&] { box.listCategories(QStringLiteral("c0")); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(result.value(QStringLiteral("categories")).toList().isEmpty());
        f.server.categoryList = "<?xml version=\"1.0\"?><categories default=\"Red category\" xmlns=\"CategoryList.xsd\">"
                                "<category name=\"Red category\" color=\"0\" keyboardShortcut=\"0\" guid=\"{a1}\"/>"
                                "<category name=\"Projects &amp; plans\" color=\"7\" guid=\"{b2}\"/></categories>";
        result = call(box, SIGNAL(categoriesFinished(QString,QVariantMap)), [&] { box.listCategories(QStringLiteral("c1")); });
        const QVariantList list = result.value(QStringLiteral("categories")).toList();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Projects & plans"));
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("preset")).toInt(), 7);
        result = call(box, SIGNAL(categoriesFinished(QString,QVariantMap)), [&] { box.addCategory(QStringLiteral("c2"), QStringLiteral("New"), 1); });
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(ExchangeTest::errorOf(result).contains(QStringLiteral("on this computer")));

        f.server.inboxRules = "<t:Rule><t:RuleId>r1</t:RuleId><t:DisplayName>Newsletters</t:DisplayName><t:Priority>1</t:Priority>"
                              "<t:IsEnabled>true</t:IsEnabled><t:Conditions><t:ContainsSubjectStrings><t:String>news</t:String>"
                              "</t:ContainsSubjectStrings></t:Conditions><t:Actions><t:MoveToFolder><t:FolderId Id=\"f\"/></t:MoveToFolder>"
                              "<t:MarkAsRead>true</t:MarkAsRead></t:Actions></t:Rule>";
        result = call(box, SIGNAL(serverRulesFinished(QString,QVariantMap)), [&] { box.listServerRules(QStringLiteral("rules")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        const QVariantMap rule = result.value(QStringLiteral("rules")).toList().value(0).toMap();
        QCOMPARE(rule.value(QStringLiteral("name")).toString(), QStringLiteral("Newsletters"));
        QCOMPARE(rule.value(QStringLiteral("summary")).toString(), QStringLiteral("subject contains news → move to a folder, mark as read"));
    }

    void ewsItemTags()
    {
        EwsTest::Fixture f;
        ExchangeMailbox box;
        ewsBox(box, f.server);
        EwsFakeServer::MailItem inbox;
        inbox.internetMessageId = QStringLiteral("<m1@example.org>");
        inbox.received = QDateTime::currentDateTimeUtc().addDays(-1);
        f.server.mail.insert(QStringLiteral("MSG1"), inbox);
        EwsFakeServer::MailItem filed = inbox;
        filed.internetMessageId = QStringLiteral("<m2@example.org>");
        filed.folderId = QStringLiteral("fld-proj");
        f.server.mail.insert(QStringLiteral("MSG2"), filed);
        f.server.mailFolders.insert(QStringLiteral("fld-proj"), QStringLiteral("Projects"));

        QVariantMap item{{QStringLiteral("kind"), QStringLiteral("message")}, {QStringLiteral("internetMessageId"), QStringLiteral("m1@example.org")},
                         {QStringLiteral("folder"), QStringLiteral("INBOX")},
                         {QStringLiteral("categories"), QStringList{QStringLiteral("Red category")}},
                         {QStringLiteral("flag"), QVariantMap{{QStringLiteral("status"), QStringLiteral("flagged")}, {QStringLiteral("due"), QStringLiteral("2026-10-16")}}},
                         {QStringLiteral("zone"), QStringLiteral("UTC")}};
        QVariantMap result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] { box.setItemState(QStringLiteral("e1"), item); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        QVERIFY(result.value(QStringLiteral("found")).toBool());
        QCOMPARE(f.server.mail.value(QStringLiteral("MSG1")).categories, QStringList{QStringLiteral("Red category")});
        QCOMPARE(f.server.mail.value(QStringLiteral("MSG1")).flagStatus, QStringLiteral("Flagged"));
        QCOMPARE(f.server.mail.value(QStringLiteral("MSG1")).due, QDateTime(QDate(2026, 10, 16), QTime(0, 0), QTimeZone::UTC));
        QCOMPARE(f.server.mail.value(QStringLiteral("MSG1")).start, QDateTime(QDate(2026, 10, 16), QTime(0, 0), QTimeZone::UTC));

        // A message in another folder: found by the folder's name first.
        item.insert(QStringLiteral("internetMessageId"), QStringLiteral("m2@example.org"));
        item.insert(QStringLiteral("folder"), QStringLiteral("Work/Projects"));
        item.insert(QStringLiteral("categories"), QStringList());
        result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] { box.setItemState(QStringLiteral("e2"), item); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        QVERIFY(f.server.operations.contains(QStringLiteral("FindFolder")));
        QCOMPARE(f.server.mail.value(QStringLiteral("MSG2")).flagStatus, QStringLiteral("Flagged"));
        item.insert(QStringLiteral("folder"), QStringLiteral("Nowhere"));
        result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] { box.setItemState(QStringLiteral("e3"), item); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool() && !result.value(QStringLiteral("found")).toBool());

        // An event's categories go without telling attendees, and come back on sync.
        QVERIFY(ExchangeTest::syncNow(f.provider, QStringLiteral("sync-0")).value(QStringLiteral("ok")).toBool());
        result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] {
            box.setItemState(QStringLiteral("e4"), {{QStringLiteral("kind"), QStringLiteral("event")}, {QStringLiteral("remoteId"), QStringLiteral("AAMkReview=")},
                                                    {QStringLiteral("categories"), QStringList{QStringLiteral("Travel")}}});
        });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(ExchangeTest::errorOf(result)));
        QCOMPARE(f.server.lastUpdateSend, QStringLiteral("SendToNone"));
        QVERIFY(ExchangeTest::syncNow(f.provider, QStringLiteral("sync-1")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.event(QStringLiteral("Design review")).value(QStringLiteral("categories")).toStringList(), QStringList{QStringLiteral("Travel")});

        // Recent inbox tags.
        result = call(box, SIGNAL(itemStateFinished(QString,QVariantMap)), [&] { box.pullItemStates(QStringLiteral("p"), 30); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        const QVariantList items = result.value(QStringLiteral("items")).toList();
        QCOMPARE(items.size(), 1);                                          // MSG2 is not in the inbox
        QCOMPARE(items.first().toMap().value(QStringLiteral("internetMessageId")).toString(), QStringLiteral("m1@example.org"));
        QCOMPARE(items.first().toMap().value(QStringLiteral("flag")).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("flagged"));
        QCOMPARE(items.first().toMap().value(QStringLiteral("categories")).toStringList(), QStringList{QStringLiteral("Red category")});
    }

    void refusalsAndSecrets()
    {
        GraphTest::Fixture f;
        ExchangeMailbox box;
        graphBox(box, f.server);
        box.setSecret(QStringLiteral("wrong-token-1"), QStringLiteral("bearer"));
        QVariantMap result = call(box, SIGNAL(autoRepliesFinished(QString,QVariantMap)), [&] { box.readAutoReplies(QStringLiteral("x")); });
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("authRejected")).toBool());
        ExchangeMailbox bare;
        QSignalSpy spy(&bare, &ExchangeMailbox::autoRepliesFinished);
        QVERIFY(!bare.readAutoReplies(QStringLiteral("none")));
        QCOMPARE(spy.count(), 0);                                           // answered on a later turn
        QVERIFY(spy.wait(2000));
        QVERIFY(spy.first().at(1).toMap().value(QStringLiteral("error")).toString().contains(QStringLiteral("credentials")));
        bare.setProperty("service", QStringLiteral("ews"));
        bare.setSecret(QStringLiteral("pw"), QStringLiteral("basic"));
        QVariantMap noUrl = call(bare, SIGNAL(categoriesFinished(QString,QVariantMap)), [&] { bare.listCategories(QStringLiteral("n")); });
        QVERIFY(ExchangeTest::errorOf(noUrl).contains(QStringLiteral("Exchange Web Services address")));
        QSignalSpy items(&box, &ExchangeMailbox::itemStateFinished);
        QVERIFY(!box.setItemState(QStringLiteral("bad"), {{QStringLiteral("kind"), QStringLiteral("message")}}));
        QVERIFY(items.wait(2000));
        ExchangeTest::seenResults().append(items.first().at(1).toMap());
        // No token or password anywhere in what the tests saw.
        QByteArray found;
        QList<QByteArray> needles = GraphTest::secrets() + EwsTest::secrets();
        for (const QVariantMap &seen : ExchangeTest::seenResults())
            QVERIFY2(ExchangeTest::clean(QJsonDocument::fromVariant(seen).toJson(), needles, &found), "secret in a result: " + found);
        QMutexLocker locker(&ExchangeTest::LogCapture::mutex());
        QVERIFY2(ExchangeTest::clean(ExchangeTest::LogCapture::lines().join(QLatin1Char('\n')).toUtf8(), needles, &found), "secret in the log: " + found);
    }
};

QTEST_GUILESS_MAIN(MailboxTest)

#include "tst_mail_exchange_mailbox.moc"
